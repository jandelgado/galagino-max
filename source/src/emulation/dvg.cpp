#include "dvg.h"

namespace dvg {

enum Opcode : uint8_t {
  OP_VCTR_MAX = 0x9, // 0x0-0x9: long vector, opcode = scale
  OP_LABS = 0xa,
  OP_HALT = 0xb,
  OP_JSRL = 0xc,
  OP_RTSL = 0xd,
  OP_JMPL = 0xe,
  OP_SVEC = 0xf,
};

static const uint16_t COORD_MASK = 0xfff;  // beam counters are 12 bit
static const uint16_t SIGN = 0x400;        // vector delta sign bit
static const uint16_t RAM_WORDS = 0x400;
static const uint16_t ROM_BASE = 0x800;    // DVG word address of vector ROM

static uint16_t fetch(const uint8_t *ram, const uint8_t *rom, uint16_t pc) {
  pc &= COORD_MASK;
  const uint8_t *p;
  if (pc < RAM_WORDS) {
    p = ram + pc * 2;
  } else if ((pc & ~(RAM_WORDS - 1)) == ROM_BASE) {
    p = rom + (pc - ROM_BASE) * 2;
  } else {
    return 0; // unmapped
  }
  return p[0] | (p[1] << 8);
}

// Rate multiplier output of MAME dvg handler_2: (2 << scale) & 0x7ff clock
// counts, each moving the beam with probability mag / 1024. The 7497 pulse
// pattern rounds half up (brute forced against handler_2: exact for all
// scales and magnitudes).
static int16_t delta(uint16_t dv, uint8_t scale, uint16_t mag_mask) {
  const int32_t len = ((int32_t)(dv & mag_mask) * ((2 << scale) & 0x7ff) + 512) >> 10;
  return (dv & SIGN) ? -len : len;
}

static int16_t sext12(uint16_t v) {
  return (int16_t)((v ^ 0x800) - 0x800);
}

uint16_t decode(const uint8_t *ram, const uint8_t *rom, vec2d::Line *out, uint16_t max) {
  uint16_t pc = 0, x = 0, y = 0;
  uint16_t stack[4];
  uint8_t sp = 0, gscale = 0;
  uint16_t n = 0;

  for (uint16_t step = 0; step < MAX_STEPS; step++) {
    const uint16_t w0 = fetch(ram, rom, pc);
    const uint8_t op = w0 >> 12;
    int16_t dx, dy;
    uint8_t intensity;

    if (op <= OP_VCTR_MAX) {
      const uint16_t w1 = fetch(ram, rom, pc + 1);
      pc += 2;
      const uint8_t scale = (op + gscale) & 0xf;
      dy = delta(w0, scale, 0x3ff);
      dx = delta(w1, scale, 0x3ff);
      intensity = w1 >> 12;
    } else if (op == OP_SVEC) {
      pc += 1;
      // nibbles: 11-8 dy, 7-4 intensity, 3-0 dx; each delta nibble holds a
      // scale bit (3), sign (2) and 2 magnitude bits, shifted up by 8
      const uint16_t dvy = ((w0 >> 8) & 0xf) << 8, dvx = (w0 & 0xf) << 8;
      const uint8_t scale = (gscale + ((dvy & 0x800) >> 11 | ((dvx & 0x800) ^ 0x800) >> 10 |
                                       (dvx & 0x800) >> 9)) & 0xf;
      dy = delta(dvy, scale, 0x300);
      dx = delta(dvx, scale, 0x300);
      intensity = (w0 >> 4) & 0xf;
    } else {
      switch (op) {
      case OP_LABS: {
        const uint16_t w1 = fetch(ram, rom, pc + 1);
        pc += 2;
        y = w0 & COORD_MASK;
        x = w1 & COORD_MASK;
        gscale = w1 >> 12;
        break;
      }
      case OP_HALT:
        return n;
      case OP_JSRL:
        stack[sp++ & 3] = pc + 1;
        pc = w0 & COORD_MASK;
        break;
      case OP_RTSL:
        pc = stack[--sp & 3];
        break;
      default: // OP_JMPL
        pc = w0 & COORD_MASK;
        break;
      }
      continue;
    }

    // draw (or move) from the current beam position
    if (intensity) {
      vec2d::Line &l = out[n++];
      l.x0 = sext12(x);
      l.y0 = sext12(y);
      l.x1 = l.x0 + dx;
      l.y1 = l.y0 + dy;
      l.color = intensity;
      if (n == max) {
        return n;
      }
    }
    x = (x + dx) & COORD_MASK;
    y = (y + dy) & COORD_MASK;
  }
  return n;
}

} // namespace dvg
