#ifdef ENABLE_VULGUS

#include "vulgus.h"
#include "vulgus_rom_main.h"
#include "vulgus_rom_audio.h"
#include "vulgus_chars.h"
#include "vulgus_tiles.h"
#include "vulgus_sprites.h"
#include "vulgus_palette.h"

static constexpr int VIS_TOP = 16;               // first visible native Y
static constexpr int VIS_END = 240;              // one past last visible native Y
static constexpr uint8_t CHAR_TRANSPARENT = 15;  // char lut entry 15 = color 47 (MAME groups)
static constexpr uint8_t SPRITE_TRANSPARENT = 15;
static constexpr uint16_t LUT_SPRITES = 0x100, LUT_TILES = 0x200;

// Unpack eagerly, largest first: not every order fits Arena's two blocks.
vulgus::vulgus()
  : vulgus_rom_main(vulgus_rom_main_blob), vulgus_rom_audio(vulgus_rom_audio_blob),
    vulgus_chars(vulgus_chars_blob), vulgus_tiles(vulgus_tiles_blob),
    vulgus_sprites(vulgus_sprites_blob), vulgus_rgb(vulgus_rgb_blob), vulgus_lut(vulgus_lut_blob) {
  tiles = vulgus_tiles.data();
  main_rom = vulgus_rom_main.data();
  sprite_gfx = vulgus_sprites.data();
  chars = vulgus_chars.data();
  audio_rom = vulgus_rom_audio.data();
  rgb = vulgus_rgb.data();
  lut = vulgus_lut.data();
}

void vulgus::reset() {
  machineBase::reset();               // clears memory[] and soundregs, resets both Z80s
  scroll_lo[0] = scroll_lo[1] = scroll_hi[0] = scroll_hi[1] = 0;
  palette_bank = 0;
  soundlatch = 0;
  ay_addr[0] = ay_addr[1] = 0;
  cmap_bank = -1;
}

void vulgus::start() {
  reset();
  game_started = 1;
}

// ---- CPUs ----

unsigned char vulgus::opZ80(unsigned short Addr) {
  if (current_cpu == 0) {
    return Addr < 0xA000 ? main_rom[Addr] : rdZ80(Addr);
  }
  return Addr < 0x2000 ? audio_rom[Addr] : rdZ80(Addr);
}

uint8_t vulgus::read_io(uint16_t addr) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xFF;
  switch (addr) {
    case 0xC000:                                         // SYSTEM
      if (keys & BUTTON_START) { v &= ~0x01; }
      if (keys & BUTTON_COIN)  { v &= ~0x80; }
      return v;
    case 0xC001:                                         // P1, 8-way
      if (keys & BUTTON_RIGHT) { v &= ~0x01; }
      if (keys & BUTTON_LEFT)  { v &= ~0x02; }
      if (keys & BUTTON_DOWN)  { v &= ~0x04; }
      if (keys & BUTTON_UP)    { v &= ~0x08; }
      if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1))  { v &= ~0x10; }   // shot
      if (keys & (BUTTON_START | BUTTON_B | BUTTON_R1)) { v &= ~0x20; }   // bomb
      return v;
    case 0xC003: return VULGUS_DSW1;
    case 0xC004: {
      uint8_t d = VULGUS_DSW2;
      if (input->demoSoundsOff()) { d &= ~VULGUS_DSW2_DEMO_SOUNDS; }
      return d;
    }
  }
  return 0xFF;                                           // P2 0xC002, unmapped
}

unsigned char vulgus::rdZ80(unsigned short Addr) {
  if (current_cpu == 1) {
    if (Addr < 0x2000) { return audio_rom[Addr]; }
    if ((Addr & 0xF800) == 0x4000) { return memory[VULGUS_SND_RAM + (Addr & 0x7FF)]; }
    if (Addr == 0x6000) { return soundlatch; }
    return 0xFF;
  }

  if (Addr < 0xA000) { return main_rom[Addr]; }
  if ((Addr & 0xF000) == 0xE000) { return memory[VULGUS_RAM + (Addr & 0xFFF)]; }
  if ((Addr & 0xF000) == 0xD000) { return memory[VULGUS_FG + (Addr & 0xFFF)]; }   // fg + bg
  if ((Addr & 0xFF80) == 0xCC00) { return memory[VULGUS_SPR + (Addr & 0x7F)]; }
  if (Addr == 0xC802 || Addr == 0xC803) { return scroll_lo[Addr & 1]; }
  if (Addr == 0xC902 || Addr == 0xC903) { return scroll_hi[Addr & 1]; }
  return read_io(Addr);
}

void vulgus::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 1) {
    if ((Addr & 0xF800) == 0x4000) { memory[VULGUS_SND_RAM + (Addr & 0x7FF)] = Value; return; }
    if ((Addr & 0xBFFE) != 0x8000) { return; }           // AY 1 at 0x8000, AY 2 at 0xC000
    const int chip = (Addr & 0x4000) ? 1 : 0;
    if (Addr & 1) {
      soundregs[16 * chip + ay_addr[chip]] = Value;
    } else {
      ay_addr[chip] = Value & 15;
    }
    return;
  }

  if ((Addr & 0xF000) == 0xE000) { memory[VULGUS_RAM + (Addr & 0xFFF)] = Value; return; }
  if ((Addr & 0xF000) == 0xD000) { memory[VULGUS_FG + (Addr & 0xFFF)] = Value; return; }
  if ((Addr & 0xFF80) == 0xCC00) { memory[VULGUS_SPR + (Addr & 0x7F)] = Value; return; }
  switch (Addr) {
    case 0xC800: soundlatch = Value; return;
    case 0xC802: case 0xC803: scroll_lo[Addr & 1] = Value; return;
    case 0xC902: case 0xC903: scroll_hi[Addr & 1] = Value; return;
    case 0xC805: palette_bank = Value & 3; return;
  }
  // 0xC804 coin counters + flip (upright): ignored
}

// ---- Frame ----

// 8 slices per frame: the sound CPU takes RST 38h after each (MAME 8x60
// Hz), the main CPU its vblank RST 10h after the last one.
void vulgus::run_frame(void) {
  for (int slice = 0; slice < VULGUS_SLICES; slice++) {
    current_cpu = 0;
    for (int i = 0; i < VULGUS_MAIN_STEPS; i++) { StepZ80(&cpu[0]); }
    current_cpu = 1;
    for (int i = 0; i < VULGUS_SND_STEPS; i++) { StepZ80(&cpu[1]); }
    IntZ80(&cpu[1], INT_RST38);
  }

  publish_video();
  current_cpu = 0;
  IntZ80(&cpu[0], INT_RST10);
}

// Latch what render reads at vblank.
void vulgus::publish_video() {
  vblank.publish([this](VideoState &v) {
    memcpy(v.fg, &memory[VULGUS_FG], sizeof(v.fg));
    memcpy(v.bg, &memory[VULGUS_BG], sizeof(v.bg));
    memcpy(v.sprites, &memory[VULGUS_SPR], sizeof(v.sprites));
    v.scroll_x = scroll_lo[1] | (scroll_hi[1] << 8);
    v.scroll_y = scroll_lo[0] | (scroll_hi[0] << 8);
    v.palette_bank = palette_bank;
  });
}

// ---- Video ----

// MAME palette(): chars 32 + lut, sprites 16 + lut, tiles lut + 64 * bank.
void vulgus::build_cmaps(uint8_t bank) {
  for (int c = 0; c < 64; c++) {
    char_opaque[c] = 0;
    for (int p = 0; p < 4; p++) {
      const uint8_t n = lut[(c << 2) | p] & 0x0F;
      cmap_chars[c][p] = rgb[32 + n];
      if (n != CHAR_TRANSPARENT) { char_opaque[c] |= 1 << p; }
    }
  }
  for (int c = 0; c < 16; c++) {
    for (int p = 0; p < 16; p++) {
      cmap_sprites[c][p] = rgb[16 + (lut[LUT_SPRITES | (c << 4) | p] & 0x0F)];
    }
  }
  for (int c = 0; c < 32; c++) {
    for (int p = 0; p < 8; p++) {
      cmap_tiles[c][p] = rgb[(lut[LUT_TILES | (c << 3) | p] & 0x0F) + 64 * bank];
    }
  }
  cmap_bank = bank;
}

void vulgus::prepare_frame(void) {
  vblank.read(video);
  if (video.palette_bank != cmap_bank) { build_cmaps(video.palette_bank); }
}

// One panel line of the bg (512x512, scan cols, 16x16) at native X x.
void IRAM_ATTR vulgus::bg_line(uint16_t *dst, int x) {
  const int tm_x = (x + video.scroll_x) & 511;
  const int col = tm_x >> 4, ix = tm_x & 15;
  int tm_y = (VIS_TOP + video.scroll_y) & 511;
  int c = 0;
  while (c < 224) {
    const uint16_t idx = col * 32 + (tm_y >> 4);
    const uint8_t attr = video.bg[0x400 + idx];
    const uint32_t (*gfx)[2] = tiles[video.bg[idx] | ((attr & 0x80) << 1)];
    const uint16_t *pal = cmap_tiles[attr & 0x1F];
    const bool flip_x = attr & 0x20, flip_y = attr & 0x40;
    const uint32_t *line = gfx[flip_x ? 15 - ix : ix];

    for (int k = tm_y & 15; k < 16 && c < 224; k++, c++) {
      const int kk = flip_y ? 15 - k : k;
      dst[c] = pal[(line[kk >> 3] >> (4 * (kk & 7))) & 7];
    }
    tm_y = (tm_y + 16) & ~15 & 511;
  }
}

// One panel line of the fg chars (32x32, scan rows, 8x8) at native X x.
void IRAM_ATTR vulgus::fg_line(uint16_t *dst, int x) {
  const int tx = x >> 3, nx = x & 7;
  for (int ty = VIS_TOP / 8; ty < VIS_END / 8; ty++) {
    const uint16_t idx = ty * 32 + tx;
    const uint8_t attr = video.fg[0x400 + idx];
    const uint8_t opaque = char_opaque[attr & 0x3F];
    if (!opaque) { continue; }
    const uint16_t bits = chars[video.fg[idx] | ((attr & 0x80) << 1)][nx];
    const uint16_t *pal = cmap_chars[attr & 0x3F];
    uint16_t *d = dst + 8 * ty - VIS_TOP;
    for (int k = 0; k < 8; k++) {
      const uint8_t p = (bits >> (2 * k)) & 3;
      if (opaque & (1 << p)) { d[k] = pal[p]; }
    }
  }
}

// Strip row 2..33 = display lines 8*(row-2)..+7 = native X 255 - line.
void IRAM_ATTR vulgus::render_row(short row) {
  if (row < 2 || row >= 34 || !game_started) { return; }

  const int x_hi = 255 - 8 * (row - 2);
  for (int l = 0; l < 8; l++) { bg_line(frame_buffer + l * 224, x_hi - l); }

  // MAME draw_sprites: last entry first, 1, 2 or 4 sprites tall
  for (int offs = VULGUS_SPRITE_BYTES - 4; offs >= 0; offs -= 4) {
    const uint8_t *s = &video.sprites[offs];
    if (s[2] == 0) { continue; }
    int n = (s[1] & 0xC0) >> 6;
    if (n == 2) { n = 3; }
    for (; n >= 0; n--) { blit_sprite(row, s[0] + n, s[1] & 0x0F, s[3], s[2] + 16 * n); }
  }

  for (int l = 0; l < 8; l++) { fg_line(frame_buffer + l * 224, x_hi - l); }
}

// Native box X [sx, sx+16), Y [sy, sy+16), pen 15 transparent, no wrap.
void IRAM_ATTR vulgus::blit_sprite(short row, int code, uint8_t color, int sx, int sy) {
  const int x_hi = 255 - 8 * (row - 2), x_lo = x_hi - 7;
  const int xa = sx > x_lo ? sx : x_lo;
  const int xb = (sx + 15) < x_hi ? (sx + 15) : x_hi;
  if (xa > xb) { return; }
  const int ya = sy > VIS_TOP ? sy : VIS_TOP;
  const int yb = (sy + 16) < VIS_END ? (sy + 16) : VIS_END;
  if (ya >= yb) { return; }

  const uint32_t (*gfx)[2] = sprite_gfx[code & 0xFF];
  const uint16_t *pal = cmap_sprites[color];
  for (int X = xa; X <= xb; X++) {
    const uint32_t *line = gfx[X - sx];
    uint16_t *dst = frame_buffer + (x_hi - X) * 224 - VIS_TOP;
    for (int Y = ya; Y < yb; Y++) {
      const int j = Y - sy;
      const uint8_t p = (line[j >> 3] >> (4 * (j & 7))) & 0x0F;
      if (p != SPRITE_TRANSPARENT) { dst[Y] = pal[p]; }
    }
  }
}

#ifdef LED_PIN
// from logos/vulgus.png: blue letters, light blue highlight, dark red shade
static const CRGB VULGUS_LED_A = CRGB(0x09a7f8);
static const CRGB VULGUS_LED_B = CRGB(0xa3ccde);
static const CRGB VULGUS_LED_C = CRGB(0x682f47);

void vulgus::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      VULGUS_LED_C, VULGUS_LED_A, VULGUS_LED_B, VULGUS_LED_A,
      VULGUS_LED_B, VULGUS_LED_A, VULGUS_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// bomb key flashes white, else the menu colors
void vulgus::gameLeds(CRGB *leds) {
  if (input->buttons_get() & BUTTON_START) {
    fill_solid(leds, NUM_LEDS, CRGB(0xffffff));
    return;
  }
  menuLeds(leds);
}
#endif

#endif // ENABLE_VULGUS
