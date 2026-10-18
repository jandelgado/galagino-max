#include "ccastles.h"
#include "ccastles_rom.h"
#include "ccastles_sprites.h"
#include "ccastles_proms.h"
#include "../../emulation/pokey.h"
#include "../../emulation/arena.h"

using namespace ccastles_video;

// memory map (MAME ccastles main_map)
enum : uint16_t {
  BITMODE_Y = 0x0001,   // 0x0000/0x0001: bitmode X/Y latch, also written to DRAM
  BITMODE = 0x0002,     // bitmode pixel access
  VRAM_END = 0x8000,
  STATIC_BASE = 0x8000, // 0x8000-0x8fff static + sprite RAM
  STATIC_END = 0x9000,
  NOVRAM_BASE = 0x9000, // 0x9000-0x93ff, 256 bytes mirrored
  NOVRAM_END = 0x9400,
  LETA_BASE = 0x9400,   // 0x9400-0x95ff
  IN0_BASE = 0x9600,    // 0x9600-0x97ff
  POKEY1_BASE = 0x9800, // 0x9800-0x99ff
  POKEY2_BASE = 0x9a00, // 0x9a00-0x9bff
  RECALL = 0x9c00,      // write blocks of 0x80 from here on
  HSCROLL = 0x9c80,
  VSCROLL = 0x9d00,
  IRQ_ACK = 0x9d80,
  WATCHDOG = 0x9e00,
  LATCH0 = 0x9e80,
  LATCH1 = 0x9f00,
  PALETTE = 0x9f80,     // 0x9f80-0x9fff
  ROM_BASE = 0xa000,
  ROM_FIXED = 0xe000,
  BANK1_OFFSET = 0x6000, // bank 1 in rom[] for 0xa000-0xdfff
  BLOCK_MASK = 0xfe00,   // 0x9000-0x9bff decode
  IO_MASK = 0xff80,      // 0x9c00-0x9fff decode
};

// POKEY register offsets beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_ALLPOT = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
static const uint8_t AUDCTL_POLY9 = 0x80;
static const uint8_t SK_RESET = 0x03; // SKCTL bits, both clear = reset

// IN0
enum : uint8_t {
  IN0_COIN1 = 0x02,  // active low
  IN0_VBLANK = 0x20, // active high
  IN0_JUMP1 = 0x40,  // left jump / 1P start, active low
};
// LETA0 joystick bits, active low
enum : uint8_t { JOY_LEFT = 0x01, JOY_DOWN = 0x02, JOY_UP = 0x04, JOY_RIGHT = 0x08 };
// 8N outputs
static const uint8_t LATCH0_COIN_COUNTERS = 0x60;
static const uint8_t LATCH0_BANK = 0x80;
// 6P outputs, data bit D3
static const uint8_t LATCH1_DATA = 0x08;
static const uint8_t LATCH1_BUF1 = 0x80;
static const uint8_t COIN_LED_FRAMES = 30;
static const uint8_t SPRITE_NONE = 0x0f;
static const uint8_t NOVRAM_BLANK = 0xff; // X2212 nvram_default

ccastles::ccastles()
  : ccastles_rom(ccastles_rom_blob),
    ccastles_sprites(ccastles_sprites_blob),
    ccastles_proms(ccastles_proms_blob) {
  memset(&m_cpu, 0, sizeof(m_cpu));
  m_cpu.read = main_read;
  m_cpu.write = main_write;
  m_cpu.user = this;
  rom = ccastles_rom.data();
  sprites = ccastles_sprites.data();
  wpprom = ccastles_proms.data();
  priprom = wpprom + 0x100;
  vram = Arena::alloc<uint8_t>(VRAM_SIZE); // ctor runs once per machine session
  for (int i = 0; i < 256; i++) {
    merge[i] = mergePen(priprom, i >> 4, i & 15);
  }
}

void ccastles::reset() {
  machineBase::reset();
  memset(vram, 0, VRAM_SIZE);
  memset(memory + NOVRAM, NOVRAM_BLANK, NOVRAM_SIZE);
  memset(pal, 0, sizeof(pal));
  memset(bitmode_xy, 0, sizeof(bitmode_xy));
  latch0 = latch1 = 0;
  hscroll = vscroll = 0;
  frame_cycles = total_cycles = 0;
  memset(random_base, 0, sizeof(random_base));
  memset(allpot, 0, sizeof(allpot));
  in_vblank = false;
  coin_leds = 0;
  m6502_reset(&m_cpu);
}

// jump and 1P start share one bit
uint8_t ccastles::in0() const {
  const unsigned int k = input->buttons_get();
  uint8_t v = (uint8_t)~IN0_VBLANK;
  if (in_vblank) {
    v |= IN0_VBLANK;
  }
  if (k & BUTTON_COIN) {
    v &= ~IN0_COIN1;
  }
  if (k & (BUTTON_FIRE | BUTTON_START)) {
    v &= ~IN0_JUMP1;
  }
  return v;
}

// LETA0: joystick; LETA1-3 unused (trackball sets only)
uint8_t ccastles::leta(uint8_t n) const {
  if (n) {
    return 0xff;
  }

  const unsigned int k = input->buttons_get();
  uint8_t v = 0xff;
  if (k & BUTTON_LEFT) {
    v &= ~JOY_LEFT;
  }
  if (k & BUTTON_DOWN) {
    v &= ~JOY_DOWN;
  }
  if (k & BUTTON_UP) {
    v &= ~JOY_UP;
  }
  if (k & BUTTON_RIGHT) {
    v &= ~JOY_RIGHT;
  }
  return v;
}

bool ccastles::pokey_running(int n) const {
  return soundregs[n * Pokey::REG_COUNT + Pokey::REG_SKCTL] & SK_RESET;
}

// ALLPOT: POKEY 2 reads IN1, POKEY 1 has no pots
uint8_t ccastles::pokey_read(int n, uint8_t reg) {
  if (reg == POKEY_ALLPOT) {
    if (pokey_running(n)) {
      allpot[n] = n ? CCASTLES_IN1 : 0;
    }
    return allpot[n];
  }
  if (reg == POKEY_RANDOM) {
    const bool poly9 = soundregs[n * Pokey::REG_COUNT + Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    const uint32_t clock = pokey_running(n) ? cpu_clock() - random_base[n] : 0;
    return Pokey::random(clock, poly9);
  }
  return 0;
}

void ccastles::pokey_write(int n, uint8_t reg, uint8_t v) {
  uint8_t *regs = soundregs + n * Pokey::REG_COUNT;
  if (reg <= POKEY_AUDCTL) {
    regs[reg] = v;
  } else if (reg == POKEY_SKCTL) {
    if (!pokey_running(n) && (v & SK_RESET)) {
      random_base[n] = cpu_clock();
    }
    regs[Pokey::REG_SKCTL] = v;
  }
}

uint8_t ccastles::read(uint16_t a) {
  if (a >= ROM_FIXED) {
    return rom[a - ROM_BASE];
  }
  if (a >= ROM_BASE) {
    return rom[((latch0 & LATCH0_BANK) ? BANK1_OFFSET : 0) + (a - ROM_BASE)];
  }
  if (a == BITMODE) {
    // addressed pixel in the upper nibble, low nibble undriven
    const uint8_t v = (uint8_t)(vram[bitmodeAddr(bitmode_xy)] << ((~bitmode_xy[0] & 1) * 4));
    autoinc(bitmode_xy, latch1);
    return v | 0x0f;
  }
  if (a < VRAM_END) {
    return vram[a];
  }
  if (a < STATIC_END) {
    return memory[STATIC_RAM + (a - STATIC_BASE)];
  }
  if (a < NOVRAM_END) {
    return memory[NOVRAM + (a & (NOVRAM_SIZE - 1))];
  }

  switch (a & BLOCK_MASK) {
    case LETA_BASE: return leta(a & 3);
    case IN0_BASE: return in0();
    case POKEY1_BASE: return pokey_read(0, a & 0x0f);
    case POKEY2_BASE: return pokey_read(1, a & 0x0f);
  }
  return 0;
}

void ccastles::write(uint16_t a, uint8_t v) {
  if (a == BITMODE) {
    // upper nibble replicated, low X bits select the pixel
    writeVram(vram, wpprom, bitmodeAddr(bitmode_xy), (v & 0xf0) | (v >> 4), WRITE_BITMODE,
              bitmode_xy[0] & 3);
    autoinc(bitmode_xy, latch1);
    return;
  }
  if (a < VRAM_END) {
    writeVram(vram, wpprom, a, v, WRITE_DIRECT, 0);
    if (a <= BITMODE_Y) {
      bitmode_xy[a] = v;
    }
    return;
  }
  if (a < STATIC_END) {
    memory[STATIC_RAM + (a - STATIC_BASE)] = v;
    return;
  }
  if (a < NOVRAM_END) {
    memory[NOVRAM + (a & (NOVRAM_SIZE - 1))] = v;
    return;
  }

  switch (a & BLOCK_MASK) {
    case POKEY1_BASE: pokey_write(0, a & 0x0f, v); return;
    case POKEY2_BASE: pokey_write(1, a & 0x0f, v); return;
  }
  if (a < ROM_BASE) {
    write_io(a, v);
  }
}

// 0x9c00-0x9fff, decoded in blocks of 0x80
void ccastles::write_io(uint16_t a, uint8_t v) {
  const uint8_t bit = 1 << (a & 7);
  switch (a & IO_MASK) {
    case RECALL: return; // SRAM keeps its contents, no EEPROM behind it
    case HSCROLL: hscroll = v; return;
    case VSCROLL: vscroll = v; return;
    case IRQ_ACK: m_cpu.irq = 0; return;
    case WATCHDOG: return;
    case LATCH0:
      latch0 = (v & 1) ? (latch0 | bit) : (latch0 & ~bit);
      if ((v & 1) && (bit & LATCH0_COIN_COUNTERS)) {
        coin_leds = COIN_LED_FRAMES;
      }
      return;
    case LATCH1:
      latch1 = (v & LATCH1_DATA) ? (latch1 | bit) : (latch1 & ~bit);
      return;
    case PALETTE:
      pal[a & 0x1f] = paletteColor(a & 0x3f, v);
      return;
  }
}

uint8_t ccastles::main_read(m6502_t *cpu, uint16_t a) {
  return static_cast<ccastles *>(cpu->user)->read(a);
}

void ccastles::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  static_cast<ccastles *>(cpu->user)->write(a, v);
}

uint32_t ccastles::cpu_clock() const {
  return total_cycles + frame_cycles;
}

void ccastles::run_until(uint32_t cycle) {
  while (frame_cycles < cycle) {
    frame_cycles += m6502_step(&m_cpu);
  }
}

// frame starts at line 0: vblank (lines 0..23) and the first IRQ. Snapshot
// at vblank end: the IRQ handler has written scroll by then.
void ccastles::run_frame() {
  for (int line = 0; line < LINES; line += LINE_STEP) {
    if (line == FIRST_LINE) {
      vblank.publish([this](VideoState &v) {
        const int buf = (latch1 & LATCH1_BUF1) ? SPRITE_BUF_SIZE : 0;
        memcpy(v.sprites, memory + SPRITE_RAM + buf, sizeof(v.sprites));
        memcpy(v.pal, pal, sizeof(v.pal));
        v.hscroll = hscroll;
        v.vscroll = vscroll;
      });
    }
    in_vblank = line < FIRST_LINE;
    if (line % IRQ_LINES == 0) {
      m_cpu.irq = 1;
    }

    run_until((uint32_t)(line + LINE_STEP) * CYCLES_PER_LINE);
  }
  frame_cycles -= CYCLES_PER_FRAME; // keep the overshoot
  total_cycles += CYCLES_PER_FRAME;
  if (!game_started) {
    game_started = 1;
  }
}

void ccastles::prepare_frame() {
  vblank.read(video);
  for (int i = 0; i < 256; i++) {
    lut[i] = video.pal[merge[i]];
  }
}

void ccastles::render_row(short row) {
  // frame_buffer is already black outside the game area
  const int strip = row - FIRST_STRIP;
  if (strip < 0 || strip >= VIS_LINES / 8) {
    return;
  }

  const int y0 = FIRST_LINE + strip * 8;
  memset(mo, SPRITE_NONE, sizeof(mo));
  drawSprites(mo, y0, video.sprites, sprites);

  // merge bitmap and sprites per pixel, MAME screen_update
  for (int l = 0; l < 8; l++) {
    int effy = (y0 + l - FIRST_LINE + video.vscroll) & 0xff;
    if (effy < FIRST_LINE) {
      effy = FIRST_LINE;
    }
    const uint8_t *src = vram + effy * LINE_BYTES;
    const uint8_t *m = mo[l];
    uint16_t *dst = frame_buffer + l * OUT_WIDTH;
    for (int x = CROP_LEFT; x < CROP_LEFT + OUT_WIDTH; x++) {
      const uint8_t ex = (uint8_t)(video.hscroll + x);
      const uint8_t pix = (src[ex >> 1] >> ((ex & 1) * 4)) & 15;
      *dst++ = lut[(m[x] << 4) | pix];
    }
  }
}

#ifdef LED_PIN
// logo colors: purple, dark purple, white
static const CRGB CCASTLES_LED_A(0x7f398d), CCASTLES_LED_B(0x471a4f), CCASTLES_LED_C(0xf8fcfd);

void ccastles::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CCASTLES_LED_A, CCASTLES_LED_B, CCASTLES_LED_C, CCASTLES_LED_C,
      CCASTLES_LED_C, CCASTLES_LED_B, CCASTLES_LED_A};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// coin: white flash. Else the menu colors rotating one LED per 8 frames.
void ccastles::gameLeds(CRGB *leds) {
  if (coin_leds) {
    coin_leds--;
    fill_solid(leds, NUM_LEDS, CRGB::White);
    return;
  }

  CRGB base[NUM_LEDS];
  menuLeds(base);
  led_frame = (led_frame + 1) % (8 * NUM_LEDS);
  const int shift = led_frame / 8;
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = base[(i + shift) % NUM_LEDS];
  }
}
#endif
