#include "bzone.h"
#include "bzone_rom.h"
#include "bzone_vrom.h"
#include "../../emulation/bzone_sound.h"
#include "../../emulation/pokey.h"

// memory map (MAME bzone_map), 15 bit address bus; VRAM 0x2000-0x2fff
// in dvgMachine::memory at VRAM_OFS
enum : uint16_t {
  RAM_END = 0x0400,
  IN0 = 0x0800,
  DSW0 = 0x0a00,
  DSW1 = 0x0c00,
  AVG_GO = 0x1200,
  MB_STATUS = 0x1800,
  MB_LO = 0x1810,
  MB_HI = 0x1818,
  POKEY_BASE = 0x1820, // 0x1820-0x182f
  SOUND = 0x1840,
  MB_GO = 0x1860, // 0x1860-0x187f, offset = command
  BZ_VRAM = 0x2000,
  BZ_VRAM_SIZE = 0x1000,
  BZ_VROM = 0x3000, // 0x3000-0x3fff
  BZ_VROM_SIZE = 0x1000,
  ROM = 0x5000, // 0x5000-0x7fff
};
// 0x1000 coin counter, 0x1400 watchdog, 0x1600 AVG reset: ignored

// IN0, active low unless noted
enum : uint8_t {
  IN0_COIN1 = 0x01,
  IN0_IDLE = 0x7f, // coins, service (off = NMI on), diag step, AVG done (high)
  IN0_CLOCK_3K = 0x80,
};
enum : uint8_t { IN3_FIRE = 0x10, IN3_START1 = 0x20 };

// POKEY registers beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_ALLPOT = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
enum : uint8_t { SK_RESET = 0x03, AUDCTL_POLY9 = 0x80 };

static_assert(BZ_SND_LATCH == Pokey::REG_COUNT, "sound latch follows the POKEY registers");

// AVG screen (MAME visarea 0..580 x 0..400), beam center
static const int32_t XCENTER = 290 << 16, YCENTER = 200 << 16;
// AVG byte address of the vector ROM
static const uint16_t AVG_ROM_BASE = BZ_VROM - BZ_VRAM;

// screen x 32..543 (Mathbox clip window 36..543) and y 0..399 scaled by
// 15/32, see bzone.h
static const vec2d::Rect VISIBLE = {32, 0, 543, 399};
static const int SCALE_MUL = 15, SCALE_SHIFT = 5;
static const int TOP_BORDER = 51;
// bzone.lay: red overlay above native y 80 -> panel row 88 = strip 11
static const uint8_t RED_STRIPS = 11;

static_assert(TOP_BORDER + ((80 * SCALE_MUL) >> SCALE_SHIFT) == RED_STRIPS * 8,
              "overlay boundary on a strip edge");

static int16_t panel_col(int16_t x) { return ((x - VISIBLE.x0) * SCALE_MUL) >> SCALE_SHIFT; }
static int16_t panel_row(int16_t y) { return TOP_BORDER + ((y * SCALE_MUL) >> SCALE_SHIFT); }

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

bzone::bzone()
  : dvgMachine(main_read, main_write, BZ_VRAM_SIZE),
    bzone_rom(bzone_rom_blob),
    bzone_vrom(bzone_vrom_blob) {
  static_assert(RAM_END <= VRAM_OFS, "Battle Zone RAM overlaps VRAM");
  static_assert(BZ_VRAM_SIZE <= MAX_VRAM_SIZE, "Battle Zone VRAM exceeds the latch");
  rom = bzone_rom.data();
  vrom = bzone_vrom.data();

  // intensity ramp as dvgMachine, tinted by the cabinet overlay
  // (bzone.lay: 1.0 / 0.125 multiply)
  palette[0] = red[0] = 0;
  for (uint8_t i = 1; i < 16; i++) {
    const uint8_t l = 80 + i * 11;
    palette[i] = rgb565(l / 8, l, l / 8);
    red[i] = rgb565(l, l / 8, l / 8);
  }
}

void bzone::reset() {
  dvgMachine::reset();
  mathbox.reset();
  avg_state.reset(XCENTER, YCENTER);
  allpot = 0;
  random_base = 0;
}

uint8_t bzone::in0() const {
  uint8_t v = IN0_IDLE;
  if (input->buttons_get() & BUTTON_COIN) {
    v &= ~IN0_COIN1;
  }
  return v | (clock_3k() ? IN0_CLOCK_3K : 0);
}

uint8_t bzone::in3() const {
  const unsigned int k = input->buttons_get();
  return bzone_treads(k) | ((k & BUTTON_FIRE) ? IN3_FIRE : 0) |
         ((k & BUTTON_START) ? IN3_START1 : 0);
}

bool bzone::pokey_running() const {
  return soundregs[Pokey::REG_SKCTL] & SK_RESET;
}

uint8_t bzone::pokey_read(uint8_t reg) {
  if (reg == POKEY_ALLPOT) {
    if (pokey_running()) {
      allpot = in3();
    }
    return allpot;
  }
  if (reg == POKEY_RANDOM) {
    const bool poly9 = soundregs[Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    return Pokey::random(pokey_running() ? cpu_cycles() - random_base : 0, poly9);
  }
  return 0;
}

void bzone::pokey_write(uint8_t reg, uint8_t v) {
  if (reg <= POKEY_AUDCTL) {
    soundregs[reg] = v;
  } else if (reg == POKEY_SKCTL) {
    if (!pokey_running() && (v & SK_RESET)) {
      random_base = cpu_cycles();
    }
    soundregs[Pokey::REG_SKCTL] = v;
  }
}

uint8_t bzone::main_read(m6502_t *cpu, uint16_t a) {
  bzone *s = static_cast<bzone *>(cpu->user);
  a &= ADDR_MASK;
  if (a >= ROM) {
    return s->rom[a - ROM];
  }
  if (a < RAM_END) {
    return s->memory[a];
  }
  if (a >= BZ_VRAM && a < BZ_VRAM + BZ_VRAM_SIZE) {
    return s->memory[VRAM_OFS + a - BZ_VRAM];
  }
  if (a >= BZ_VROM && a < BZ_VROM + BZ_VROM_SIZE) {
    return s->vrom[a - BZ_VROM];
  }
  if ((a & ~0x0f) == POKEY_BASE) {
    return s->pokey_read(a & 0x0f);
  }
  switch (a) {
    case IN0: return s->in0();
    case DSW0: return BZONE_DSW0;
    case DSW1: return BZONE_DSW1;
    case MB_STATUS: return 0; // done
    case MB_LO: return s->mathbox.lo();
    case MB_HI: return s->mathbox.hi();
  }
  return 0;
}

void bzone::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  bzone *s = static_cast<bzone *>(cpu->user);
  a &= ADDR_MASK;
  if (a < RAM_END) {
    s->memory[a] = v;
    return;
  }
  if (a >= BZ_VRAM && a < BZ_VRAM + BZ_VRAM_SIZE) {
    s->memory[VRAM_OFS + a - BZ_VRAM] = v;
    return;
  }
  if ((a & ~0x0f) == POKEY_BASE) {
    s->pokey_write(a & 0x0f, v);
    return;
  }
  if ((a & ~0x1f) == MB_GO) {
    s->mathbox.go(a & 0x1f, v);
    return;
  }
  switch (a) {
    case AVG_GO: s->go(); break;
    case SOUND: s->soundregs[BZ_SND_LATCH] = v; break;
  }
}

uint16_t bzone::decode_frame(const uint8_t *vram) {
  const avg::Memory m = {vram, BZ_VRAM_SIZE, vrom, AVG_ROM_BASE, BZ_VROM_SIZE};
  return avg::decode(m, avg_state, lines, MAX_LINES);
}

// Clips lines to the visible area and turns them into panel coordinates,
// keeping only visible ones. Returns their count.
uint16_t bzone::map_lines(uint16_t count) {
  uint16_t kept = 0;
  for (uint16_t i = 0; i < count; i++) {
    vec2d::Line l = lines[i];
    if (!vec2d::clip(l, VISIBLE)) {
      continue;
    }
    vec2d::Line &o = lines[kept++];
    o.x0 = panel_col(l.x0);
    o.y0 = panel_row(l.y0);
    o.x1 = panel_col(l.x1);
    o.y1 = panel_row(l.y1);
    o.color = l.color;
  }
  return kept;
}

const uint16_t *bzone::strip_palette(uint8_t strip) const {
  return strip < RED_STRIPS ? red : palette;
}

#ifdef LED_PIN
void bzone::menuLeds(CRGB *leds) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}

void bzone::gameLeds(CRGB *leds) {
  menuLeds(leds);
}
#endif
