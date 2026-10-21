#include "../../catalog.h"
#ifdef GAME_PLEIADS

#include "pleiads.h"
#include "pleiads_rom.h"
#include "pleiads_tiles.h"
#include "pleiads_palette.h"

static constexpr int LOOPS_PER_FRAME = 2500;   // x 4 instructions, as phoenix
static constexpr int DISPLAY_LOOPS = 2100;     // vblank after ~84 % (phoenix)
static constexpr int VIS_Y = 208;              // native lines 0..207
static constexpr int COL_OFFSET = 8;           // (224 - 208) / 2
static constexpr uint16_t PEN_FG = 0x20;       // MAME: fg colors | 0x08 (x 4 pens)

pleiads::pleiads()
  : pleiads_rom(pleiads_rom_blob), pleiads_bgtiles(pleiads_bgtiles_blob),
    pleiads_fgtiles(pleiads_fgtiles_blob), pleiads_palette(pleiads_palette_blob) {
  rom = pleiads_rom.data();
  bg_gfx = pleiads_bgtiles.data();
  fg_gfx = pleiads_fgtiles.data();
  palette = pleiads_palette.data();
}

void pleiads::reset() {
  machineBase::reset();
  page = palette_bank = scroll = protection = 0;
  vblank_active = false;
}

void pleiads::start() {
  reset();
  game_started = 1;
}

// ---- CPU ----

unsigned char pleiads::opZ80(unsigned short Addr) {
  return Addr < 0x4000 ? rom[Addr] : rdZ80(Addr);
}

// MAME pleiads_protection_r on IN0 bit 3, an IP_ACTIVE_LOW field: answer
// 1 reads as 0. The other polarity desyncs the attract demo from MAME.
static uint8_t protection_bit(uint8_t question) {
  return (question == 0x0C || question == 0x30) ? 0 : 1;
}

// IN0: b0 coin, b1 start 1, b2 start 2, b3 protection, b4-7 CTRL (fire,
// right, left, unused), all active low
uint8_t pleiads::read_in0() {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xF7 | (protection_bit(protection) << 3);
  if (keys & BUTTON_COIN)  { v &= ~0x01; }
  if (keys & BUTTON_START) { v &= ~0x02; }
  if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_X)) { v &= ~0x10; }
  if (keys & BUTTON_RIGHT) { v &= ~0x20; }
  if (keys & BUTTON_LEFT)  { v &= ~0x40; }
  return v;
}

unsigned char pleiads::rdZ80(unsigned short Addr) {
  if (Addr < 0x4000) { return rom[Addr]; }
  if (Addr < 0x5000) { return memory[page * PAGE + (Addr & 0xFFF)]; }
  if ((Addr & 0xFC00) == 0x7000) { return read_in0(); }
  if ((Addr & 0xFC00) == 0x7800) {
    uint8_t d = PLEIADS_DSW0;
    if (input->demoSoundsOff()) { d &= ~PLEIADS_DSW0_DEMO_SOUNDS; }
    return d | (vblank_active ? 0x00 : 0x80);
  }
  return 0xFF;
}

// MAME pleiads_videoreg_w: b0 page, b1-2 palette bank, b2-7 protection
// question, the whole byte goes to sound latch C
void pleiads::write_videoreg(uint8_t v) {
  page = v & 1;
  palette_bank = (v >> 1) & 3;
  protection = v & 0xFC;
  soundregs[2] = v;
}

void pleiads::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr < 0x4000) { return; }
  if (Addr < 0x5000) { memory[page * PAGE + (Addr & 0xFFF)] = Value; return; }
  switch (Addr & 0xFC00) {
    case 0x5000: write_videoreg(Value); return;
    case 0x5800: scroll = Value; return;
    case 0x6000: soundregs[0] = Value; return;   // sound latch A
    case 0x6800: soundregs[1] = Value; return;   // sound latch B, TMS3615 note
  }
}

// ---- Frame ----

// No vblank IRQ: the game polls DSW0 bit 7. Video is latched where vblank
// starts.
void pleiads::run_frame(void) {
  vblank_active = false;
  for (int i = 0; i < LOOPS_PER_FRAME; i++) {
    if (i == DISPLAY_LOOPS) {
      publish_video();
      vblank_active = true;
    }
    StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu);
  }
}

void pleiads::publish_video() {
  vblank.publish([this](VideoState &v) {
    const uint8_t *p = &memory[page * PAGE];
    memcpy(v.fg, p, MAP_BYTES);
    memcpy(v.bg, p + 0x800, MAP_BYTES);
    v.scroll = scroll;
    v.palette_bank = palette_bank;
  });
}

// ---- Video ----

void pleiads::prepare_frame(void) {
  vblank.read(video);
}

// One panel line (native X x) of a 32x32 tilemap: columns walk native Y
// from 207 down to 0. Pen = bank << 6 | fg << 5 | (code >> 5) << 2 | p.
void IRAM_ATTR pleiads::layer_line(uint16_t *dst, const uint8_t *map, const uint16_t (*gfx)[8],
                                   int x, uint16_t pen_base, bool transparent) {
  const int tx = x >> 3, nx = x & 7;
  uint16_t *d = dst + COL_OFFSET + VIS_Y - 1;          // column of native Y 0
  for (int ty = 0; ty < VIS_Y / 8; ty++, d -= 8) {
    const uint8_t code = map[ty * 32 + tx];
    const uint16_t bits = gfx[code][nx];
    if (transparent && !bits) { continue; }
    const uint16_t *pal = &palette[pen_base | ((code >> 5) << 2)];
    for (int k = 0; k < 8; k++) {
      const uint8_t p = (bits >> (2 * k)) & 3;
      if (p || !transparent) { d[-k] = pal[p]; }
    }
  }
}

// Strip row 2..33 = native X 8 * (row - 2) .. +7.
void IRAM_ATTR pleiads::render_row(short row) {
  if (row < 2 || row >= 34) { return; }

  const uint16_t bank = video.palette_bank << 6;
  for (int l = 0; l < 8; l++) {
    const int x = 8 * (row - 2) + l;
    uint16_t *dst = frame_buffer + l * 224;
    layer_line(dst, video.bg, bg_gfx, (x + video.scroll) & 0xFF, bank, false);
    layer_line(dst, video.fg, fg_gfx, x, bank | PEN_FG, true);
  }
}

#ifdef LED_PIN
// from logos/pleiads.png: blue letters, cyan and purple highlights
static const CRGB PLEIADS_LED_A = CRGB(0x1d079d);
static const CRGB PLEIADS_LED_B = CRGB(0x3bbbc7);
static const CRGB PLEIADS_LED_C = CRGB(0x603061);

void pleiads::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      PLEIADS_LED_C, PLEIADS_LED_B, PLEIADS_LED_A, PLEIADS_LED_A,
      PLEIADS_LED_A, PLEIADS_LED_B, PLEIADS_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// cyan while the noise generator plays (latch A bit 7, explosions)
void pleiads::gameLeds(CRGB *leds) {
  if (soundregs[0] & 0x80) {
    fill_solid(leds, NUM_LEDS, PLEIADS_LED_B);
    return;
  }
  menuLeds(leds);
}
#endif

#endif // GAME_PLEIADS
