#include "../../catalog.h"
#ifdef GAME_MEGAZONE

#include "megazone.h"
#include "megazone_rom_main.h"
#include "megazone_rom_sound.h"
#include "megazone_rom_i8039.h"
#include "megazone_chars.h"
#include "megazone_sprites.h"
#include "megazone_palette.h"

static constexpr uint16_t MAIN_ROM_BASE = 0x6000;
static constexpr int VIS_TOP = 16;       // first visible native Y
static constexpr int VIS_END = 240;      // one past last visible native Y
static constexpr int WIDTH = 224;        // panel columns = visible native Y
static constexpr int PANEL_END = 48;     // native X of the first playfield column
static constexpr int NATIVE_W = 288;
static constexpr int SCROLL_X_BASE = 32; // MAME: scrollx = 32 - reg 0x1000

// one frame = 256 lines: main 1.536 MHz, Z80 3.072 MHz, i8039 7.16 MHz / 15
static constexpr int LINES = 256;
static constexpr int MAIN_CYCLES_LINE = 100;
static constexpr int Z80_STEPS_LINE = 17;      // ~200 cycles (mikie calibration)
static constexpr int Z80_CYCLES_LINE = 200;
static constexpr int32_t I8039_HZ = 7159090 / 15;
static constexpr int SAMPLE_RATE = 24000;

// MAME mcs48.cpp burn_cycles() per opcode
static const uint8_t I8039_CYCLES[256] = {
    1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

// sound CPU inputs, active low
static constexpr uint8_t IN0_COIN1 = 0x01, IN0_START1 = 0x08;
static constexpr uint8_t IN1_LEFT = 0x01, IN1_RIGHT = 0x02, IN1_UP = 0x04, IN1_DOWN = 0x08;
static constexpr uint8_t IN1_FIRE = 0x10;
static constexpr uint8_t AY_PORT_A = 14;
static constexpr uint8_t I8039_INT_ENABLE = 0x80;   // P2 b7 = 0 clears INT
static constexpr int DAC_GAIN = 2;                  // (P1 - 128) * 2 next to the AY

megazone::megazone()
  : megazone_rom_main(megazone_rom_main_blob), megazone_rom_sound(megazone_rom_sound_blob),
    megazone_rom_i8039(megazone_rom_i8039_blob), megazone_chars(megazone_chars_blob),
    megazone_sprites(megazone_sprites_blob), megazone_rgb(megazone_rgb_blob),
    megazone_lut(megazone_lut_blob) {
  // m6809_reset() reads the reset vector through main_rom
  main_rom = megazone_rom_main.data();
  sound_rom = megazone_rom_sound.data();
  i8039_rom = megazone_rom_i8039.data();
  chars = megazone_chars.data();
  sprite_gfx = megazone_sprites.data();

  // MAME palette(): sprite pen = rgb[lut[c<<4|p]], char pen = rgb[lut[0x100|c<<4|p]];
  // a sprite pen whose lookup is color 0 is transparent (transpen_mask)
  const uint16_t *rgb = megazone_rgb.data();
  const uint8_t *lut = megazone_lut.data();
  for (int c = 0; c < 16; c++) {
    sprite_opaque[c] = 0;
    for (int p = 0; p < 16; p++) {
      const uint8_t s = lut[(c << 4) | p];
      cmap_sprites[c][p] = rgb[s];
      cmap_chars[c][p] = rgb[lut[0x100 | (c << 4) | p]];
      sprite_opaque[c] |= s ? 1u << p : 0;
    }
  }
}

void megazone::reset() {
  machineBase::reset();   // clears memory[], resets the Z80
  irq_mask = 0;
  scroll_x = scroll_y = 0;
  sound_latch = ay_addr = 0;
  i8039_status = 0;
  snd_cycles = 0;
  i8039_debt = 0;
  dac_wr = dac_rd = 0;
  dac_val = dac_last = 0x80;
  visible_count = 0;

  m6809_reset(&main_cpu);
  main_cpu.rom_direct = main_rom;   // must follow m6809_reset()
  main_cpu.rom_base = MAIN_ROM_BASE;
  main_cpu.rom_size = 0x10000 - MAIN_ROM_BASE;
  main_cpu.konami1 = 1;

  i8048_reset(&i8039);
  i8039.rom_direct = i8039_rom;
  i8039.rom_mask = 0x0fff;
}

void megazone::start() {
  reset();
  game_started = 1;
}

// ---- Main KONAMI-1 ----

// KONAMI-1: opcodes XORed by address bits 1 and 3 (circusc, rocnrope)
unsigned char IRAM_ATTR megazone::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  if (addr >= MAIN_ROM_BASE) {
    const uint8_t mask = ((addr & 0x02) ? 0x80 : 0x20) | ((addr & 0x08) ? 0x08 : 0x02);
    return main_rom[addr - MAIN_ROM_BASE] ^ mask;
  }
  return m6809_read(s, addr);
}

unsigned char IRAM_ATTR megazone::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= MAIN_ROM_BASE) {
    return main_rom[addr - MAIN_ROM_BASE];
  }
  if (addr >= 0x2000 && addr < 0x4000) {
    return (addr & 0x1c00) == 0x1400 ? 0 : memory[addr - 0x2000];   // 3400-37ff unmapped
  }
  return 0;   // write-only registers, debug ROM socket 4000-5fff
}

void IRAM_ATTR megazone::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr >= 0x2000 && addr < 0x4000) {
    memory[addr - 0x2000] = val;
    return;
  }
  if (addr < 0x0008) {
    // LS259: Q7 vblank IRQ enable; Q0/Q1 coin counters, Q5 flip (upright): ignored
    if (addr == 7) {
      irq_mask = val & 1;
      if (!irq_mask) {
        main_cpu.irq_pending = 0;
      }
    }
    return;
  }
  if (addr == 0x1000) {
    scroll_y = val;
    return;
  }
  if (addr == 0x1800) {
    scroll_x = val;
  }
  // 0x0800 watchdog: ignored
}

// ---- Sound Z80 ----

unsigned char megazone::opZ80(unsigned short Addr) {
  return Addr < 0x2000 ? sound_rom[Addr] : rdZ80(Addr);
}

unsigned char megazone::rdZ80(unsigned short Addr) {
  if (Addr < 0x2000) {
    return sound_rom[Addr];
  }
  if (Addr >= 0xe000 && Addr < 0xe800) {
    return memory[MEGAZONE_SHARED + (Addr & 0x7ff)];
  }
  return soundRead(Addr);
}

uint8_t megazone::soundRead(uint16_t addr) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  switch (addr) {
    case 0x6000:
      if (keys & BUTTON_COIN) { v &= ~IN0_COIN1; }
      if (keys & BUTTON_START) { v &= ~IN0_START1; }
      return v;
    case 0x6001:
      if (keys & BUTTON_LEFT) { v &= ~IN1_LEFT; }
      if (keys & BUTTON_RIGHT) { v &= ~IN1_RIGHT; }
      if (keys & BUTTON_UP) { v &= ~IN1_UP; }
      if (keys & BUTTON_DOWN) { v &= ~IN1_DOWN; }
      if (keys & (BUTTON_FIRE | BUTTON_A)) { v &= ~IN1_FIRE; }
      return v;
    case 0x8000:
      return MEGAZONE_DSW2 | (input->demoSoundsOff() ? MEGAZONE_DSW2_DEMO_SOUND_OFF : 0);
    case 0x8001:
      return MEGAZONE_DSW1;
  }
  return 0xff;   // 0x6002 player 2
}

void megazone::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0xe000 && Addr < 0xe800) {
    memory[MEGAZONE_SHARED + (Addr & 0x7ff)] = Value;
    return;
  }
  if (Addr == 0x2000) {
    i8039.notINT = false;   // ASSERT_LINE until P2 b7 = 0
    return;
  }
  if (Addr == 0x4000) {
    sound_latch = Value;
  }
  // a000, c000, c001 watchdog: ignored
}

// MAME port_a_r: AY clock / 1024 counted in Z80 cycles (14.318/8 vs 18.432/6
// MHz), b4-7, plus the i8039 status in b0-2
uint8_t megazone::ayPortA(void) {
  const uint32_t clock = (uint32_t)((uint64_t)snd_cycles * 7159 / 12288);
  return (uint8_t)((((clock / 512) & 0x0f) << 4) | i8039_status);
}

unsigned char megazone::inZ80(unsigned short Port) {
  if ((Port & 0xff) > 2) {
    return 0xff;
  }
  return ay_addr == AY_PORT_A ? ayPortA() : soundregs[ay_addr];
}

void megazone::outZ80(unsigned short Port, unsigned char Value) {
  const uint8_t p = Port & 0xff;
  if (p == 0x00) {
    ay_addr = Value & 0x0f;
    return;
  }
  if (p == 0x02) {
    soundregs[ay_addr] = Value;   // port B: RC filter caps, ignored
  }
}

// ---- i8039: P1 = DAC, P2 b7 INT clear / b4-6 status, any MOVX = latch ----

void megazone::wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char val) {
  if (port == 1) {
    dac_val = val;
    return;
  }
  if (port == 2) {
    if (!(val & I8039_INT_ENABLE)) {
      i8039.notINT = true;
    }
    i8039_status = (val & 0x70) >> 4;
  }
}

unsigned char megazone::rdI8048_port(struct i8048_state_S *state, unsigned char port) {
  return 0xff;
}

unsigned char megazone::rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) {
  return sound_latch;
}

unsigned char megazone::rdI8048_rom(struct i8048_state_S *state, unsigned short addr) {
  return i8039_rom[addr & 0x0fff];
}

// i8039 for this many cycles, the DAC sampled every 1 / 24 kHz in between
void megazone::runI8039(int32_t cycles) {
  i8039_debt += cycles * SAMPLE_RATE;
  while (i8039_debt > 0) {
    int32_t used = 0;
    while (used * SAMPLE_RATE < I8039_HZ) {
      const uint8_t op = i8039_rom[i8039.PC & 0x0fff];
      i8048_step(&i8039);
      used += I8039_CYCLES[op];
    }
    i8039_debt -= used * SAMPLE_RATE;

    const uint16_t next = (dac_wr + 1) & (DAC_RING - 1);
    if (next != dac_rd) {   // full: drop
      dac_ring[dac_wr] = dac_val;
      dac_wr = next;
    }
  }
}

// ---- Frame ----

// vblank: IRQ to main (if enabled) and Z80 (MAME HOLD_LINE), then 256
// lines of both CPUs; the i8039 runs along in slices of 16 lines
void megazone::run_frame(void) {
  publishVideo();
  if (irq_mask) {
    m6809_irq(&main_cpu);
  }
  bool z80_irq = true;

  int main_debt = 0;
  for (int line = 0; line < LINES; line++) {
    main_debt += MAIN_CYCLES_LINE;
    while (main_debt > 0) {
      const int c = m6809_step(&main_cpu, 4);
      main_debt -= c > 0 ? c : 1;
    }

    if (z80_irq && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      z80_irq = false;
    }
    for (int i = 0; i < Z80_STEPS_LINE; i++) {
      StepZ80(&cpu[0]);
    }
    snd_cycles += Z80_CYCLES_LINE;

    if ((line & 15) == 15) {
      runI8039(I8039_HZ / 60 / (LINES / 16));
    }
  }
}

// Latch what render reads, where MAME raises vblank.
void megazone::publishVideo(void) {
  vblank.publish([this](VideoState &v) {
    memcpy(v.vram0, &memory[MEGAZONE_VRAM0], sizeof(v.vram0));
    memcpy(v.cram0, &memory[MEGAZONE_CRAM0], sizeof(v.cram0));
    for (int ty = 0; ty < 32; ty++) {
      memcpy(&v.vram1[ty * PANEL_COLS], &memory[MEGAZONE_VRAM1 + ty * 32], PANEL_COLS);
      memcpy(&v.cram1[ty * PANEL_COLS], &memory[MEGAZONE_CRAM1 + ty * 32], PANEL_COLS);
    }
    memcpy(v.sprites, &memory[MEGAZONE_SPRITES], sizeof(v.sprites));
    v.scroll_x = scroll_x;
    v.scroll_y = scroll_y;
  });
}

// ---- Audio ----

void megazone::renderAudio(int16_t *out, int n) {
  Ay8910::renderBank(ay, soundregs, out, n);
  for (int i = 0; i < n; i++) {
    if (dac_rd != dac_wr) {
      dac_last = dac_ring[dac_rd];
      dac_rd = (dac_rd + 1) & (DAC_RING - 1);
    }
    out[i] = audioClamp(out[i] + dac_dc.filter(((int)dac_last - 0x80) * DAC_GAIN), AUDIO_MAX);
  }
}

// ---- Video ----

// sprites on screen right of the panel, in MAME draw order (last on top)
void megazone::prepare_frame(void) {
  vblank.read(video);
  visible_count = 0;
  for (int offs = SPRITE_BYTES - 4; offs >= 0; offs -= 4) {
    const uint8_t *s = &video.sprites[offs];
    const int sx = s[3] + 32;
    const int sy = 255 - ((s[1] + 16) & 0xff);
    if (sx + 16 <= PANEL_END || sy + 16 <= VIS_TOP || sy >= VIS_END) {
      continue;
    }
    visible[visible_count++] = offs;
  }
}

// one native column X (= panel line) of a tilemap with stride tiles per
// row: tile column tx, pixel column nx in the tile, native Y shifted by sy
// (scroll), 8 rows per tile
void IRAM_ATTR megazone::drawTileColumn(const uint8_t *vram, const uint8_t *cram, uint8_t stride,
                                        uint8_t tx, uint8_t nx, uint8_t sy, uint16_t *dst) {
  int y = VIS_TOP;
  while (y < VIS_END) {
    const uint8_t by = (uint8_t)(y + sy);
    const uint8_t k0 = by & 7;
    const int n = (8 - k0) < (VIS_END - y) ? (8 - k0) : (VIS_END - y);

    const uint16_t idx = (by >> 3) * stride + tx;
    const uint8_t attr = cram[idx];
    const uint32_t *gfx = chars[vram[idx] | ((attr & 0x80) << 1)];
    const uint16_t *pal = cmap_chars[attr & 0x0f];
    const bool flip_x = attr & 0x40, flip_y = attr & 0x20;
    const uint32_t bits = gfx[flip_x ? 7 - nx : nx];

    uint16_t *d = dst + (VIS_END - 1 - y);
    for (int k = k0; k < k0 + n; k++, d--) {
      const int kk = flip_y ? 7 - k : k;
      *d = pal[(bits >> (4 * kk)) & 0x0f];
    }
    y += n;
  }
}

// strip row = native X 8*row..8*row+7, one panel line each
void IRAM_ATTR megazone::render_row(short row) {
  if (row < 0 || row >= NATIVE_W / 8 || !game_started) {
    return;
  }

  if (row < PANEL_COLS) {
    for (int l = 0; l < 8; l++) {
      drawTileColumn(video.vram1, video.cram1, PANEL_COLS, row, l, 0, frame_buffer + l * WIDTH);
    }
    return;
  }

  for (int l = 0; l < 8; l++) {
    const uint8_t bx = (uint8_t)(8 * row + l + video.scroll_y - SCROLL_X_BASE);
    drawTileColumn(video.vram0, video.cram0, 32, bx >> 3, bx & 7, video.scroll_x,
                   frame_buffer + l * WIDTH);
  }
  for (int i = 0; i < visible_count; i++) {
    drawSprite(row, &video.sprites[visible[i]]);
  }
}

// MAME draw_sprites: native box [sx, sx+16) x [sy, sy+16), no wrap, pens
// with lookup color 0 transparent
void IRAM_ATTR megazone::drawSprite(short row, const uint8_t *s) {
  const int sx = s[3] + 32;
  const int sy = 255 - ((s[1] + 16) & 0xff);
  const int x_lo = 8 * row, x_hi = x_lo + 7;
  const int xa = sx > x_lo ? sx : x_lo;
  const int xb = sx + 15 < x_hi ? sx + 15 : x_hi;
  if (xa > xb) {
    return;
  }
  const int ya = sy > VIS_TOP ? sy : VIS_TOP;
  const int yb = sy + 16 < VIS_END ? sy + 16 : VIS_END;

  const uint8_t (*gfx)[8] = sprite_gfx[s[2]];
  const uint8_t color = s[0] & 0x0f;
  const uint16_t *pal = cmap_sprites[color];
  const uint16_t opaque = sprite_opaque[color];
  const bool flip_x = !(s[0] & 0x40), flip_y = s[0] & 0x80;

  for (int x = xa; x <= xb; x++) {
    const int i = flip_x ? 15 - (x - sx) : x - sx;
    uint16_t *dst = frame_buffer + (x - x_lo) * WIDTH + (VIS_END - 1);
    for (int y = ya; y < yb; y++) {
      const int j = flip_y ? 15 - (y - sy) : y - sy;
      const uint8_t p = (gfx[j][i >> 1] >> ((i & 1) << 2)) & 0x0f;
      if (opaque & (1u << p)) {
        dst[-y] = pal[p];
      }
    }
  }
}

#ifdef LED_PIN
// logo colors (logos/megazone.png): blue letters, dark blue shade
static const CRGB MEGAZONE_LED_A(0x4080c0), MEGAZONE_LED_B(0x1b265b);

void megazone::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      MEGAZONE_LED_B, MEGAZONE_LED_A, MEGAZONE_LED_A, MEGAZONE_LED_A,
      MEGAZONE_LED_A, MEGAZONE_LED_A, MEGAZONE_LED_B};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// bright while FIRE is held
void megazone::gameLeds(CRGB *leds) {
  if (input->buttons_get() & BUTTON_FIRE) {
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = MEGAZONE_LED_A;
    }
    return;
  }
  menuLeds(leds);
}
#endif

#endif // GAME_MEGAZONE
