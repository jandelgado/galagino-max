#include "../../catalog.h"
#ifdef GAME_ARKANOID

#include "arkanoid.h"
#include "arkanoid_rom.h"
#include "arkanoid_tiles.h"
#include "arkanoid_palette.h"

static constexpr uint16_t ROM_END = 0xC000;
static constexpr uint16_t MAIN_STEPS = 8 * INST_PER_FRAME;   // 6 MHz, as the 1943 main CPU
static constexpr int FIRST_ROW = 2;        // panel line 16 = native X 0
static constexpr int LAST_ROW = 34;        // one past native X 255
static constexpr int VIS_TOP = 16;         // first visible native Y
static constexpr int VIS_BOTTOM = 239;     // native Y of panel column 0
static constexpr int TILE_COLS = 28;       // panel tile columns = native tile rows 29..2
static constexpr int SPRITES = 16;
static constexpr int SPRITE_Y_BASE = 248;  // MAME: sy = 248 - s[1]
static constexpr uint8_t CTRL_GFX_BANK = 0x20, CTRL_PAL_BANK = 0x40, CTRL_PADDLE_P2 = 0x04;
static constexpr uint8_t PEN_MASK = 0x07;
// SYSTEM d00c: b0 START1 (active low), b4 COIN1 (active high), b6 bootleg
// check bit 1, other bits idle
static constexpr uint8_t SYSTEM_IDLE = 0x4F, SYSTEM_START1 = 0x01, SYSTEM_COIN1 = 0x10;
static constexpr uint8_t BUTTONS_IDLE = 0xFF, BUTTONS_FIRE1 = 0x01;
static constexpr uint8_t AY_PORT_A = 14, AY_PORT_B = 15;

arkanoid::arkanoid()
  : arkanoid_rom(arkanoid_rom_blob), arkanoid_tiles(arkanoid_tiles_blob),
    arkanoid_tile_map(arkanoid_tile_map_blob), arkanoid_rgb(arkanoid_rgb_blob) {
  rom = arkanoid_rom.data();
  tiles = arkanoid_tiles.data();
  tile_map = arkanoid_tile_map.data();
  rgb = arkanoid_rgb.data();
}

void arkanoid::reset() {
  machineBase::reset();
  irq_pending = false;
  ctrl = 0;
  ay_addr = 0;
  paddle = Spinner(SPIN_MIN, SPIN_MAX, SPIN_ACCEL);
}

void arkanoid::start() {
  reset();
  game_started = 1;
}

// ---- CPU ----

unsigned char arkanoid::opZ80(unsigned short Addr) {
  return Addr < ROM_END ? rom[Addr] : rdZ80(Addr);
}

unsigned char arkanoid::rdZ80(unsigned short Addr) {
  if (Addr < ROM_END) { return rom[Addr]; }
  if ((Addr & 0xF800) == 0xC000) { return memory[ARKANOID_RAM + (Addr & 0x7FF)]; }
  if ((Addr & 0xF000) == 0xE000) { return memory[ARKANOID_VRAM + (Addr & 0xFFF)]; }
  if (Addr >= 0xF000) { return 0x00; }   // MAME: reads 0, else instant death in the last level

  const unsigned int keys = input->buttons_get();
  switch (Addr) {
    case 0xD001:
      if (ay_addr == AY_PORT_A) { return 0xFF; }
      if (ay_addr == AY_PORT_B) { return ARKANOID_DSW; }
      return soundregs[ay_addr];
    case 0xD008: return 0x00;   // bootleg kludge read, 0 for arkangc
    case 0xD00C: {
      uint8_t v = SYSTEM_IDLE;
      if (keys & BUTTON_START) { v &= ~SYSTEM_START1; }
      if (keys & BUTTON_COIN)  { v |= SYSTEM_COIN1; }
      return v;
    }
    case 0xD010: {
      uint8_t v = BUTTONS_IDLE;
      if (keys & (BUTTON_FIRE | BUTTON_A)) { v &= ~BUTTONS_FIRE1; }
      return v;
    }
    case 0xD018: return paddle.count();   // P1 and P2 (d008 b2) share the knob
  }
  return 0xFF;
}

void arkanoid::wrZ80(unsigned short Addr, unsigned char Value) {
  if ((Addr & 0xF800) == 0xC000) { memory[ARKANOID_RAM + (Addr & 0x7FF)] = Value; return; }
  if ((Addr & 0xF000) == 0xE000) { memory[ARKANOID_VRAM + (Addr & 0xFFF)] = Value; return; }
  switch (Addr) {
    case 0xD000: ay_addr = Value & 0x0F; return;
    case 0xD001: soundregs[ay_addr] = Value; return;
    case 0xD008: ctrl = Value; return;   // flip (upright: ignored), banks
  }
  // d010 watchdog, d018 bootleg command: ignored
}

// ---- Frame ----

void arkanoid::run_frame(void) {
  const unsigned int keys = input->buttons_get();
  paddle.step((keys & BUTTON_RIGHT) ? 1 : (keys & BUTTON_LEFT) ? -1 : 0);

  for (int i = 0; i < MAIN_STEPS; i++) {
    // irq0_line_hold: taken once the game enables interrupts
    if (irq_pending && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      irq_pending = false;
    }
    StepZ80(&cpu[0]);
  }

  publish_video();
  irq_pending = true;
}

void arkanoid::publish_video() {
  vblank.publish([this](VideoState &v) {
    memcpy(v.vram, &memory[ARKANOID_VRAM], sizeof(v.vram));
    memcpy(v.sprites, &memory[ARKANOID_SPR], sizeof(v.sprites));
    v.ctrl = ctrl;
  });
}

// ---- Video ----

void arkanoid::prepare_frame(void) {
  vblank.read(video);
}

// Strip row 2..33 = native X 8 * (row - 2) .. +7 (one native tile column).
void GAME_IRAM arkanoid::render_row(short row) {
  if (row < FIRST_ROW || row >= LAST_ROW) { return; }

  const int tx = row - FIRST_ROW;
  const uint16_t bank = (video.ctrl & CTRL_GFX_BANK) ? 2048 : 0;
  const uint16_t *pal_bank = rgb + ((video.ctrl & CTRL_PAL_BANK) ? 256 : 0);
  for (int tc = 0; tc < TILE_COLS; tc++) {
    const int ty = (VIS_BOTTOM >> 3) - tc;
    const uint8_t *v = &video.vram[2 * (ty * 32 + tx)];
    const uint32_t *gfx = tiles[tile_map[(v[1] | ((v[0] & 0x07) << 8)) + bank]];
    const uint16_t *pal = pal_bank + 8 * (v[0] >> 3);
    uint16_t *dst = frame_buffer + 8 * tc;
    for (int r = 0; r < 8; r++, dst += WIDTH) {
      const uint32_t bits = gfx[r];
      for (int x = 0; x < 8; x++) { dst[x] = pal[(bits >> (28 - 4 * x)) & PEN_MASK]; }
    }
  }

  for (int s = 0; s < SPRITES; s++) { blit_sprite(row, &video.sprites[4 * s]); }
}

// 8 wide (native X) x 16 tall: char 2 * code at native Y sy - 8, char
// 2 * code + 1 at sy. Pen 0 transparent, clipped to the visible area.
void GAME_IRAM arkanoid::blit_sprite(short row, const uint8_t *s) {
  const int x0 = 8 * (row - FIRST_ROW);
  const int sx = s[0];
  const int ra = sx > x0 ? sx : x0;
  const int rb = (sx + 8) < (x0 + 8) ? (sx + 8) : (x0 + 8);
  if (ra >= rb) { return; }

  const int sy = SPRITE_Y_BASE - s[1];
  const uint16_t code = 2 * (s[3] | ((s[2] & 0x03) << 8)) + ((video.ctrl & CTRL_GFX_BANK) ? 2048 : 0);
  const uint16_t *pal = rgb + ((video.ctrl & CTRL_PAL_BANK) ? 256 : 0) + 8 * (s[2] >> 3);
  for (int half = 0; half < 2; half++) {
    const int y0 = sy - 8 + 8 * half;   // native Y of the char's first row
    const uint32_t *gfx = tiles[tile_map[code + half]];
    const int c0 = VIS_BOTTOM - 7 - y0;   // panel column of pixel 0
    for (int X = ra; X < rb; X++) {
      const uint32_t bits = gfx[X - sx];
      uint16_t *dst = frame_buffer + (X - x0) * WIDTH;
      for (int x = 0; x < 8; x++) {
        const int c = c0 + x;
        if (c < 0 || c >= WIDTH) { continue; }
        const uint8_t p = (bits >> (28 - 4 * x)) & PEN_MASK;
        if (p) { dst[c] = pal[p]; }
      }
    }
  }
}

#ifdef LED_PIN
// from logos/arkanoid.png: red outline, salmon and white letters
static const CRGB ARKANOID_LED_A = CRGB(0x9a2e27);
static const CRGB ARKANOID_LED_B = CRGB(0xe78c79);
static const CRGB ARKANOID_LED_C = CRGB(0xfffdfc);

void arkanoid::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      ARKANOID_LED_A, ARKANOID_LED_B, ARKANOID_LED_C, ARKANOID_LED_C,
      ARKANOID_LED_C, ARKANOID_LED_B, ARKANOID_LED_A};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// FIRE (launch / laser) flashes white, else the menu colors
void arkanoid::gameLeds(CRGB *leds) {
  if (input->buttons_get() & BUTTON_FIRE) {
    fill_solid(leds, NUM_LEDS, ARKANOID_LED_C);
    return;
  }
  menuLeds(leds);
}
#endif

#endif // GAME_ARKANOID
