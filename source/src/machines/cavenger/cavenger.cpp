#include "../../catalog.h"
#ifdef GAME_CAVENGER

#include "cavenger.h"
#include "cavenger_rom.h"
#include "cavenger_chars.h"
#include "cavenger_sprites.h"
#include "cavenger_palette.h"

// action key: START with a COIN pin or the galagino controller, else EXTRA
// (without a COIN pin BUTTON_START only follows a virtual coin, 600 ms late)
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int ACTION_KEY = BUTTON_START;
#else
static const unsigned int ACTION_KEY = BUTTON_EXTRA;
#endif

static constexpr int VIS_LEFT = 8;          // first visible native X
static constexpr int VIS_TOP = 32;          // first visible native Y
static constexpr int FIRST_ROW = 6;         // strip of native Y 32: (288 - 192) / 2 / 8
static constexpr int LAST_ROW = 30;         // one past the strip of native Y 223
static constexpr int VBLANK_STEP = 936;     // of INST_PER_FRAME: ~75 % active display (ladybug)
static constexpr uint8_t SPR_ENABLE = 0x80, SPR_BIG = 0x40, SPR_FLIPX = 0x20, SPR_FLIPY = 0x10;

cavenger::cavenger()
  : cavenger_rom(cavenger_rom_blob), cavenger_chars(cavenger_chars_blob),
    cavenger_sprites(cavenger_sprites_blob), cavenger_small(cavenger_small_blob),
    cavenger_rgb(cavenger_rgb_blob), cavenger_lut(cavenger_lut_blob) {
  rom = cavenger_rom.data();
  chars = cavenger_chars.data();
  sprite_gfx = cavenger_sprites.data();
  small_gfx = cavenger_small.data();
  rgb = cavenger_rgb.data();
  lut = cavenger_lut.data();

  // MAME ladybug_palette(): char pen p of color c = indirect p << 3 | c,
  // sprite pen p of color c = lut[c << 2 | p] (bit-reversed nibble)
  for (int c = 0; c < 8; c++) {
    for (int p = 0; p < 4; p++) { cmap_chars[c][p] = rgb[(p << 3) | c]; }
  }
  for (int c = 0; c < 16; c++) {
    for (int p = 0; p < 4; p++) { cmap_sprites[c][p] = rgb[lut[(c << 2) | p]]; }
  }
}

void cavenger::reset() {
  machineBase::reset();
  vblank_active = 0;
  coin_prev = 0;
}

void cavenger::start() {
  reset();
  game_started = 1;
}

// ---- CPU ----

unsigned char cavenger::opZ80(unsigned short Addr) {
  return Addr < 0x6000 ? rom[Addr] : rdZ80(Addr);
}

// IN0 9000, IN1 9001 (vblank), DSW0 9002, DSW1 9003, IN2 E000 (button 2).
// Coin 1 raises the NMI on its rising edge.
uint8_t cavenger::read_input(uint16_t addr) {
  const unsigned int keys = input->buttons_get();
  const uint8_t coin = (keys & BUTTON_COIN) ? 1 : 0;
  if (coin && !coin_prev) { IntZ80(&cpu[0], INT_NMI); }
  coin_prev = coin;

  uint8_t v = 0xFF;
  switch (addr) {
    case 0x9000:
      if (keys & BUTTON_LEFT)  { v &= ~0x01; }
      if (keys & BUTTON_DOWN)  { v &= ~0x02; }
      if (keys & BUTTON_RIGHT) { v &= ~0x04; }
      if (keys & BUTTON_UP)    { v &= ~0x08; }
      if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1)) { v &= ~0x10; }   // fire
      if (keys & BUTTON_START) { v &= ~0x20; }
      return v;
    case 0x9001:
      return vblank_active ? 0xBF : 0x7F;              // b6 active low, b7 active high
    case 0x9002: return CAVENGER_DSW0;
    case 0x9003: return CAVENGER_DSW1;
    case 0xE000:
      if (keys & (ACTION_KEY | BUTTON_B | BUTTON_R1)) { v &= ~0x01; }  // bomb
      return v;
  }
  return 0xFF;
}

unsigned char cavenger::rdZ80(unsigned short Addr) {
  if (Addr < 0x6000) { return rom[Addr]; }
  if ((Addr & 0xF000) == 0x6000) { return memory[CAVENGER_RAM + (Addr & 0xFFF)]; }
  if ((Addr & 0xFC00) == 0x7000) { return memory[CAVENGER_SPR + (Addr & 0x3FF)]; }
  if ((Addr & 0xF800) == 0xD000) { return memory[CAVENGER_BG + (Addr & 0x7FF)]; }
  if ((Addr & 0xFFFC) == 0x9000 || Addr == 0xE000) { return read_input(Addr); }
  return 0xFF;
}

void cavenger::wrZ80(unsigned short Addr, unsigned char Value) {
  if ((Addr & 0xF000) == 0x6000) { memory[CAVENGER_RAM + (Addr & 0xFFF)] = Value; return; }
  if ((Addr & 0xFC00) == 0x7000) { memory[CAVENGER_SPR + (Addr & 0x3FF)] = Value; return; }
  if ((Addr & 0xF800) == 0xD000) { memory[CAVENGER_BG + (Addr & 0x7FF)] = Value; return; }
  if ((Addr & 0xF000) == 0xB000) { sn[0].write(Value); return; }
  if ((Addr & 0xF000) == 0xC000) { sn[1].write(Value); return; }
  // 0xA000 flip (upright): ignored
}

// ---- Frame ----

// No vblank IRQ: the game polls IN1. Video is latched where vblank starts.
void cavenger::run_frame(void) {
  vblank_active = 0;
  for (int i = 0; i < INST_PER_FRAME; i++) {
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    if (i == VBLANK_STEP) {
      publish_video();
      vblank_active = 1;
    }
  }
}

void cavenger::publish_video() {
  vblank.publish([this](VideoState &v) {
    memcpy(v.bg, &memory[CAVENGER_BG], sizeof(v.bg));
    memcpy(v.sprites, &memory[CAVENGER_SPR], sizeof(v.sprites));
  });
}

// ---- Video ----

void cavenger::prepare_frame(void) {
  vblank.read(video);
}

// One panel line of the bg at native Y y: 32x32 chars, per tile row
// scroll from bg RAM (MAME ladybug_video_device::draw), pen 0 = color 0.
void GAME_IRAM cavenger::bg_line(uint16_t *dst, int y) {
  const int ty = y >> 3, iy = y & 7;
  const uint8_t scroll = video.bg[((ty & 3) << 5) | (ty >> 2)];
  int tm_x = (VIS_LEFT + scroll) & 255;
  int c = 0;
  while (c < WIDTH) {
    const uint16_t idx = ty * 32 + (tm_x >> 3);
    const uint8_t attr = video.bg[0x400 + idx];
    const uint16_t bits = chars[video.bg[idx] | ((attr & 0x08) << 5)][iy];
    const uint16_t *pal = cmap_chars[attr & 0x07];
    for (int k = tm_x & 7; k < 8 && c < WIDTH; k++, c++) {
      const uint8_t p = (bits >> (2 * k)) & 3;
      dst[c] = p ? pal[p] : rgb[0];
    }
    tm_x = (tm_x + 8) & 0xF8;
  }
}

// Strip row 6..29 = native Y 32 + 8 * (row - 6) .. +7.
void GAME_IRAM cavenger::render_row(short row) {
  if (row < FIRST_ROW || row >= LAST_ROW) { return; }

  const int y0 = VIS_TOP + 8 * (row - FIRST_ROW);
  for (int l = 0; l < 8; l++) { bg_line(frame_buffer + l * WIDTH, y0 + l); }

  // MAME draw_sprites: blocks 0x380 down to 0x80, each from its last
  // non-zero entry down to entry 0
  for (int offs = 0x400 - 0x80; offs >= 0x80; offs -= 0x40) {
    int i = 0;
    while (i < 0x40 && video.sprites[offs + i]) { i += 4; }
    while (i > 0) {
      i -= 4;
      if (video.sprites[offs + i] & SPR_ENABLE) { blit_sprite(row, &video.sprites[offs + i], offs); }
    }
  }
}

// Big sprites 16x16 at (x, y - 8), small 8x8 at (x, y); pen 0 transparent.
void GAME_IRAM cavenger::blit_sprite(short row, const uint8_t *s, int block) {
  const bool big = s[0] & SPR_BIG, flip_x = s[0] & SPR_FLIPX, flip_y = s[0] & SPR_FLIPY;
  const int size = big ? 16 : 8;
  const int x = s[3];
  const int y = ((block >> 2) | (s[0] & 0x0F)) - (big ? 8 : 0);
  const int y0 = VIS_TOP + 8 * (row - FIRST_ROW);
  const int ya = y > y0 ? y : y0;
  const int yb = (y + size) < (y0 + 8) ? (y + size) : (y0 + 8);
  if (ya >= yb) { return; }

  const uint16_t code = s[1] | ((s[2] & 0x10) << 4);
  const uint16_t *pal = cmap_sprites[s[2] & 0x0F];
  for (int Y = ya; Y < yb; Y++) {
    const int j = flip_y ? size - 1 - (Y - y) : Y - y;
    const uint32_t bits = big ? sprite_gfx[code >> 2][j] : small_gfx[code][j];
    uint16_t *dst = frame_buffer + (Y - y0) * WIDTH - VIS_LEFT;
    for (int i = 0; i < size; i++) {
      const int X = x + i;
      if (X < VIS_LEFT || X >= VIS_LEFT + WIDTH) { continue; }
      const int ii = flip_x ? size - 1 - i : i;
      const uint8_t p = (bits >> (2 * ii)) & 3;
      if (p) { dst[X] = pal[p]; }
    }
  }
}

#ifdef LED_PIN
// from logos/cavenger.png: blue, orange and magenta letters
static const CRGB CAVENGER_LED_A = CRGB(0x00aeff);
static const CRGB CAVENGER_LED_B = CRGB(0xffae00);
static const CRGB CAVENGER_LED_C = CRGB(0xff00ff);

void cavenger::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CAVENGER_LED_C, CAVENGER_LED_B, CAVENGER_LED_A, CAVENGER_LED_A,
      CAVENGER_LED_A, CAVENGER_LED_B, CAVENGER_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// bomb key flashes orange, else the menu colors
void cavenger::gameLeds(CRGB *leds) {
  if (input->buttons_get() & BUTTON_START) {
    fill_solid(leds, NUM_LEDS, CAVENGER_LED_B);
    return;
  }
  menuLeds(leds);
}
#endif

#endif // GAME_CAVENGER
