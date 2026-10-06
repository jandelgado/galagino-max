#include "avg.h"

namespace avg {

enum Opcode : uint8_t {
  OP_VCTR = 0, OP_HALT = 1, OP_SVEC = 2, OP_STAT = 3,
  OP_CNTR = 4, OP_JSRL = 5, OP_RTSL = 6, OP_JMPL = 7,
};

static const int16_t WINDOW_FAR = 1280; // MAME vg_flush 0x5000000 >> 16

void State::reset(int32_t xc, int32_t yc) {
  *this = State{};
  xcenter = xpos = xc;
  ycenter = ypos = yc;
  hst = lst = true;
}

static uint8_t fetch(const Memory &m, uint16_t a) {
  if (a < m.ram_bytes) {
    return m.ram[a];
  }
  if ((uint16_t)(a - m.rom_base) < m.rom_bytes) {
    return m.rom[a - m.rom_base];
  }
  return 0; // unmapped
}

// MAME avg_strobe0 normalization plus avg_strobe1 binary scale: returns
// the timer, dvx/dvy shifted until bit 12 differs from bit 11
static uint16_t normalize(uint16_t &dvx, uint16_t &dvy, uint8_t bin_scale, bool op1) {
  uint16_t timer = 0;
  for (int i = 0; i < 16 && !((dvy ^ (dvy << 1)) & 0x1000) && !((dvx ^ (dvx << 1)) & 0x1000); i++) {
    dvy = (dvy & 0x1000) | ((dvy << 1) & 0x1fff);
    dvx = (dvx & 0x1000) | ((dvx << 1) & 0x1fff);
    timer = (timer >> 1) | 0x4000 | (op1 << 7);
  }
  if (op1) {
    timer &= 0xff;
  }
  for (uint8_t i = bin_scale; i > 0; i--) {
    timer = (timer >> 1) | 0x4000 | (op1 << 7);
  }
  if (op1) {
    timer &= 0xff;
  }
  return timer;
}

// MAME avg_strobe3 deflection: int32 product, wrapping like MAME's int
// arithmetic (unsigned multiply, defined in C++)
static int32_t deflect(uint16_t dv, uint32_t cycles, uint8_t scale) {
  const int32_t dac = (int32_t)(((dv >> 3) ^ 0x200) & 0x3ff) - 0x200;
  return (int32_t)((uint32_t)dac * cycles * (uint32_t)(scale ^ 0xff)) >> 4;
}

uint16_t decode(const Memory &mem, State &s, vec2d::Line *out, uint16_t max) {
  uint16_t pc = 0, stack[4] = {0, 0, 0, 0};
  uint8_t sp = 0;
  uint16_t n = 0;
  vec2d::Rect window = {0, 0, WINDOW_FAR, WINDOW_FAR};

  for (uint16_t step = 0; step < MAX_STEPS && n < max; step++) {
    // h1: high byte of word 0; Battle Zone clip latch on the way
    const uint8_t hi = fetch(mem, pc + 1);
    if (!s.hst) {
      s.clipx_max = s.xpos;
      s.clipy_min = s.ypos;
    }
    if (!s.lst) {
      s.clipx_min = s.xpos;
      s.clipy_max = s.ypos;
    }
    if (!s.hst || !s.lst) {
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
      const uint8_t lo = fetch(mem, pc); // h3 on the low byte
      int_latch = lo >> 4;
      dvx = ((int_latch & 1) << 12) | ((lo & 0xf) << 8);
      pc += 2;
    } else {
      dvy |= fetch(mem, pc); // h0
      pc += 2;
      if (op == OP_VCTR) {
        const uint8_t hi1 = fetch(mem, pc + 1), lo1 = fetch(mem, pc); // h3, h2
        int_latch = hi1 >> 4;
        dvx = ((int_latch & 1) << 12) | ((hi1 & 0xf) << 8) | lo1;
        pc += 2;
      }
    }

    switch (op) {
      case OP_VCTR:
      case OP_SVEC: {
        const uint16_t timer = normalize(dvx, dvy, s.bin_scale, op1);
        const uint32_t cycles = op1 ? 0x100 - (timer & 0xff) : 0x8000 - timer;
        const int32_t x0 = s.xpos, y0 = s.ypos;
        s.xpos += deflect(dvx, cycles, s.scale);
        s.ypos -= deflect(dvy, cycles, s.scale);
        const uint8_t intensity = (int_latch >> 1) == 1 ? s.intensity : (int_latch & 0xe);
        if (intensity) {
          vec2d::Line l = {};
          l.x0 = x0 >> 16;
          l.y0 = y0 >> 16;
          l.x1 = s.xpos >> 16;
          l.y1 = s.ypos >> 16;
          l.color = intensity;
          if (vec2d::clip(l, window)) {
            out[n++] = l;
          }
        }
        break;
      }
      case OP_HALT:
        return n;
      case OP_STAT: // h6
        if (!dvy12) {
          s.intensity = (dvy >> 4) & 0xf;
          if (!(dvy & 0x400)) {
            s.lst = dvy & 0x200;
            s.hst = !s.lst;
          }
        } else {
          s.scale = dvy & 0xff;
          s.bin_scale = (dvy >> 8) & 7;
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
        break;
      case OP_JMPL:
        pc = dvy << 1;
        break;
    }
  }
  return n;
}

} // namespace avg
