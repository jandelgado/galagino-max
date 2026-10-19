#include "avg.h"

namespace avg {

// -Os (framework flags) leaves these per step helpers as calls
#define AVG_INLINE inline __attribute__((always_inline))

enum Opcode : uint8_t {
  OP_VCTR = 0, OP_HALT = 1, OP_SVEC = 2, OP_STAT = 3,
  OP_CNTR = 4, OP_JSRL = 5, OP_RTSL = 6, OP_JMPL = 7,
};

static const int16_t WINDOW_FAR = 1280; // MAME vg_flush 0x5000000 >> 16

// Draw time = vector time (normalize()) plus a fixed cost per instruction.
// MAME adds 8 clocks per state machine step; 32 per instruction matches
// its Black Widow redraw rate (1.4 vs 1.33 GOs per frame on the screen
// after game over).
static const uint32_t DRAW_CLOCKS_PER_STEP = 32;

void State::reset(int32_t xc, int32_t yc) {
  *this = State{};
  xcenter = xpos = xc;
  ycenter = ypos = yc;
  hst = lst = true;
}

static const uint16_t BANK_BASE = 0x2000, BANK_MASK = 0x1fff; // Major Havoc
static const uint8_t BANK_SHIFT = 13;

static AVG_INLINE uint8_t fetch(const Memory &m, const State &s, uint16_t a) {
  if (a < m.ram_bytes) {
    return m.ram[a];
  }
  if ((uint16_t)(a - m.rom_base) < m.rom_bytes) {
    return m.rom[a - m.rom_base];
  }
  if (m.banked && a >= BANK_BASE) {
    return m.banked[(s.page << BANK_SHIFT) | (a & BANK_MASK)];
  }
  return 0; // unmapped
}

// bits 11-0 of 13 bit v equal to sign bit 12, counted from bit 11; 16:
// v is 0 and never normalizes
static AVG_INLINE uint8_t sign_run(uint16_t v) {
  const uint16_t w = (v ^ ((v & 0x1000) ? 0xfff : 0)) & 0xfff;
  if (w) {
    return __builtin_clz(w) - 20;
  }
  return (v & 0x1000) ? 12 : 16;
}

// MAME avg_strobe0 normalization plus avg_strobe1 binary scale: shifts
// dvx/dvy until bit 12 differs from bit 11 (at most 16 times) and returns
// the draw time in cycles, halved per shift and binary scale step
static AVG_INLINE uint32_t normalize(uint16_t &dvx, uint16_t &dvy, uint8_t bin_scale, bool op1) {
  const uint8_t kx = sign_run(dvx), ky = sign_run(dvy);
  const uint8_t k = kx < ky ? kx : ky;
  dvx = (dvx & 0x1000) | ((dvx << k) & 0xfff);
  dvy = (dvy & 0x1000) | ((dvy << k) & 0xfff);
  const uint32_t cycles = (op1 ? 0x100u : 0x8000u) >> (k + bin_scale);
  return cycles ? cycles : 1;
}

// both endpoints inside r: clipping would change nothing
static AVG_INLINE bool inside(const vec2d::Line &l, const vec2d::Rect &r) {
  return l.x0 >= r.x0 && l.x0 <= r.x1 && l.x1 >= r.x0 && l.x1 <= r.x1 &&
         l.y0 >= r.y0 && l.y0 <= r.y1 && l.y1 >= r.y0 && l.y1 <= r.y1;
}

// MAME bitswap<4>(v, b3, b2, b1, b0)
static AVG_INLINE uint8_t bitswap4(uint8_t v, uint8_t b3, uint8_t b2, uint8_t b1, uint8_t b0) {
  return (((v >> b3) & 1) << 3) | (((v >> b2) & 1) << 2) | (((v >> b1) & 1) << 1) | ((v >> b0) & 1);
}

// MAME mhavoc_strobe3: the sparkle LFSR steps once per point (8 cycles);
// stepped at most once around its 7 bit cycle
static void sparkle_steps(State &s, uint32_t cycles) {
  static const uint8_t MAX_STEPS = 127, FULL = 0x7f;
  uint32_t n = cycles / 8;
  for (n = n < MAX_STEPS ? n : MAX_STEPS; n; n--) {
    s.lfsr = (((s.lfsr >> 6) ^ (s.lfsr >> 5) ^ 1) & 1) | (s.lfsr << 1);
    if ((s.lfsr & FULL) == FULL) {
      s.lfsr = 0;
    }
  }
}

// Line::color of a vector with VCTR/SVEC intensity field z
static AVG_INLINE uint8_t line_color(const Memory &m, const State &s, uint8_t z) {
  if (m.variant == STARWARS) {
    // MAME starwars_strobe3: z * STAT intensity / 8, 0..222 -> level 1..14;
    // black lines are skipped like moves
    const uint16_t level = (((z >> 1) * s.intensity) >> 3) + 15;
    return level < 16 || !s.color ? 0 : (s.color << SW_COLOR_SHIFT) | (level >> 4);
  }
  const uint8_t level = (z >> 1) == 1 ? s.intensity : (z & 0xe);
  if (m.variant == COLOR) {
    // MAME avg_strobe3: color111(STAT color), black lines skipped
    return level && s.color ? (s.color << SW_COLOR_SHIFT) | level : 0;
  }
  if (m.variant != TEMPEST && m.variant != MHAVOC) {
    return level;
  }

  // MAME tempest_strobe3: color RAM bits are inverted, 0xf is black.
  // Major Havoc sparkle (mhavoc_strobe3) takes a color per point from color
  // RAM 15..30; one color per line here, the first point's.
  static const uint8_t BLACK = 0xf, SPARKLE_COLORS = 0xf;
  const uint8_t index = s.sparkle ? SPARKLE_COLORS + bitswap4(s.lfsr, 0, 2, 4, 6) : s.color;
  const uint8_t value = m.colorram[index] & 0xf;
  if (!level || value == BLACK) {
    return 0;
  }
  const uint8_t l = (level + 1) >> 1;
  return (value << TP_COLOR_SHIFT) | (l < TP_LEVEL_MASK ? l : TP_LEVEL_MASK);
}

// MAME avg_strobe2 STAT (color), bzone_strobe2, starwars_strobe2 and tempest_strobe2 variants
static void stat(const Memory &m, State &s, uint16_t dvy) {
  if (m.variant == MHAVOC) {
    // MAME mhavoc_strobe2; the sparkle seed's bits 6-4 come from the alpha
    // CPU address bus in hardware, random in MAME
    static const uint16_t SPARKLE = 0x800, XFLIP = 0x400;
    s.color = dvy & 0xf;
    s.intensity = (dvy >> 4) & 0xf;
    s.page = (dvy >> 8) & 3;
    s.sparkle = dvy & SPARKLE;
    if (s.sparkle) {
      s.lfsr = bitswap4(dvy, 0, 1, 2, 3) | ((s.seed++ & 7) << 4);
    }
    s.xflip = dvy & XFLIP;
    return;
  }

  if (m.variant == TEMPEST) {
    // MAME tempest_strobe2: bit 11 selects color RAM index or intensity
    if (dvy & 0x800) {
      s.color = dvy & 0xf;
    } else {
      s.intensity = (dvy >> 4) & 0xf;
    }
    return;
  }

  if (m.variant == STARWARS) {
    s.intensity = dvy & 0xff;
    s.color = (dvy >> 8) & 7;
    return;
  }

  s.intensity = (dvy >> 4) & 0xf;
  if (m.variant == COLOR) {
    s.color = dvy & 7;
    return;
  }
  if (!(dvy & 0x400)) {
    s.lst = dvy & 0x200;
    s.hst = !s.lst;
  }
}

// MAME avg_strobe3 deflection: int32 product, wrapping like MAME's int
// arithmetic (unsigned multiply, defined in C++). Major Havoc flips X by
// xoring the DAC input with 0x1ff instead of 0x200.
static const uint16_t DAC_XOR = 0x200, DAC_XOR_FLIP = 0x1ff;
static AVG_INLINE int32_t deflect(uint16_t dv, uint32_t cycles, uint8_t scale, uint16_t dac_xor = DAC_XOR) {
  const int32_t dac = (int32_t)(((dv >> 3) ^ dac_xor) & 0x3ff) - 0x200;
  return (int32_t)((uint32_t)dac * cycles * (uint32_t)(scale ^ 0xff)) >> 4;
}

uint16_t decode(const Memory &mem, State &s, vec2d::Line *out, uint16_t max) {
  // byte offsets of a word's high (opcode) and low byte
  const uint8_t hi_ofs = mem.variant == STARWARS ? 0 : 1, lo_ofs = hi_ofs ^ 1;
  uint16_t pc = 0, stack[4] = {0, 0, 0, 0};
  uint8_t sp = 0;
  uint16_t n = 0;
  vec2d::Rect window = {0, 0, WINDOW_FAR, WINDOW_FAR};
  uint8_t glyph_sp = 0; // stack depth inside a glyph routine, 0: outside
  bool in_run = false;
  int16_t run_x = 0, run_y = 0;
  s.draw_clocks = 0;

  for (uint16_t step = 0; step < MAX_STEPS && n < max; step++) {
    s.draw_clocks += DRAW_CLOCKS_PER_STEP;
    // h1: high byte of word 0; Battle Zone and Major Havoc clip latch on the way
    const uint8_t hi = fetch(mem, s, pc + hi_ofs);
    if (mem.variant == MHAVOC) {
      // MAME mhavoc_latch1: only the window top
      if (!s.lst) {
        window.y0 = s.ypos >> 16;
      }
    } else if (!s.hst || !s.lst) {
      if (!s.hst) {
        s.clipx_max = s.xpos;
        s.clipy_min = s.ypos;
      }
      if (!s.lst) {
        s.clipx_min = s.xpos;
        s.clipy_max = s.ypos;
      }
      const int16_t x0 = s.clipx_min >> 16, x1 = s.clipx_max >> 16;
      const int16_t y0 = s.clipy_min >> 16, y1 = s.clipy_max >> 16;
      window.x0 = x0 < x1 ? x0 : x1;
      window.x1 = x0 < x1 ? x1 : x0;
      window.y0 = y0 < y1 ? y0 : y1;
      window.y1 = y0 < y1 ? y1 : y0;
    }
    s.hst = s.lst = true;

    const uint8_t op = hi >> 5;
    const bool op1 = op & 2;
    uint16_t dvy = (((hi >> 4) & 1) << 12) | ((hi & 0xf) << 8);
    const bool dvy12 = hi & 0x10;
    uint16_t dvx = 0;
    uint8_t int_latch = 0;

    if (op == OP_SVEC) {
      const uint8_t lo = fetch(mem, s, pc + lo_ofs); // h3 on the low byte
      int_latch = lo >> 4;
      dvx = ((int_latch & 1) << 12) | ((lo & 0xf) << 8);
      pc += 2;
    } else {
      dvy |= fetch(mem, s, pc + lo_ofs); // h0
      pc += 2;
      if (op == OP_VCTR) {
        const uint8_t hi1 = fetch(mem, s, pc + hi_ofs), lo1 = fetch(mem, s, pc + lo_ofs); // h3, h2
        int_latch = hi1 >> 4;
        dvx = ((int_latch & 1) << 12) | ((hi1 & 0xf) << 8) | lo1;
        pc += 2;
      }
    }

    if (!glyph_sp) {
      const uint16_t t = dvy << 1; // JSRL target
      if (op == OP_JSRL && t >= mem.glyph_lo && t < mem.glyph_hi) {
        if (!in_run) {
          run_x = s.xpos >> 16;
          run_y = s.ypos >> 16;
          in_run = true;
        }
        glyph_sp = sp + 1; // depth after the push below
      } else if (op != OP_STAT && op != OP_VCTR && op != OP_SVEC) {
        in_run = false;
      }
    }

    switch (op) {
      case OP_VCTR:
      case OP_SVEC: {
        const uint32_t cycles = normalize(dvx, dvy, s.bin_scale, op1);
        s.draw_clocks += cycles;
        const int32_t x0 = s.xpos, y0 = s.ypos;
        s.xpos += deflect(dvx, cycles, s.scale, s.xflip ? DAC_XOR_FLIP : DAC_XOR);
        s.ypos -= deflect(dvy, cycles, s.scale);
        const uint8_t intensity = line_color(mem, s, int_latch);
        if (s.sparkle) {
          sparkle_steps(s, cycles);
        }
        if (intensity) {
          vec2d::Line l = {};
          l.x0 = x0 >> 16;
          l.y0 = y0 >> 16;
          l.x1 = s.xpos >> 16;
          l.y1 = s.ypos >> 16;
          l.color = intensity;
          if (inside(l, window) || vec2d::clip(l, window)) {
            if (glyph_sp) {
              l.color |= GLYPH;
              l.dx = run_x;
              l.dy = run_y;
            }
            out[n++] = l;
          }
        }
        break;
      }
      case OP_HALT:
        return n;
      case OP_STAT: // h6
        if (!dvy12) {
          stat(mem, s, dvy);
        } else {
          s.scale = dvy & 0xff;
          s.bin_scale = (dvy >> 8) & 7;
          if (mem.variant == MHAVOC && (dvy & 0x800)) {
            s.lst = false; // mhavoc_strobe2: clip at the next latch1
          }
        }
        break;
      case OP_CNTR:
        s.xpos = s.xcenter;
        s.ypos = s.ycenter;
        break;
      case OP_JSRL:
        stack[sp & 3] = pc;
        sp = (sp + 1) & 0xf;
        pc = dvy << 1;
        break;
      case OP_RTSL:
        sp = (sp - 1) & 0xf;
        pc = stack[sp & 3];
        if (sp < glyph_sp) {
          glyph_sp = 0; // back at caller level
        }
        break;
      case OP_JMPL:
        if (mem.variant == TEMPEST && !dvy) {
          return n; // frame end (MAME avg_common_strobe2)
        }
        pc = dvy << 1;
        break;
    }
  }
  return n;
}

} // namespace avg
