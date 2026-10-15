// Astrocade video bus: function generator and pattern board (MAME astrocde_v.cpp).
#ifndef ASTROCADE_BUS_H
#define ASTROCADE_BUS_H

#include <stdint.h>

namespace astrocade {

struct FuncGen {
  uint8_t control = 0, expand[2] = {0, 0}, expandCount = 0, rotateCount = 0;
  uint8_t rotateData[4] = {}, prev = 0, intercept = 0;

  // port 0x0c
  void setControl(uint8_t d) {
    control = d;
    expandCount = 0;
    rotateCount = 0;
    prev = 0;
  }
  // port 0x19
  void setExpand(uint8_t d) {
    expand[0] = d & 3;
    expand[1] = (d >> 2) & 3;
  }

  // astrocade_funcgen_w. Returns false while the rotate accumulates (first 4
  // writes); else *out = byte to store at 0x4000 + offset. `old` is the byte
  // there, only used when control & 0x30.
  bool apply(uint8_t data, uint8_t old, uint8_t *out) {
    if (control & 0x08) {
      expandCount ^= 1;
      data >>= 4 * expandCount;
      data = (expand[(data >> 3) & 1] << 6) | (expand[(data >> 2) & 1] << 4) |
             (expand[(data >> 1) & 1] << 2) | (expand[(data >> 0) & 1] << 0);
    }
    const uint8_t p = prev;
    prev = data;

    if (control & 0x04) {
      if ((rotateCount & 4) == 0) {
        rotateData[rotateCount++ & 3] = data;
        return false;
      }
      const uint8_t shift = 2 * (~rotateCount++ & 3);
      data = (((rotateData[3] >> shift) & 3) << 6) | (((rotateData[2] >> shift) & 3) << 4) |
             (((rotateData[1] >> shift) & 3) << 2) | (((rotateData[0] >> shift) & 3) << 0);
    } else {
      const uint8_t shift = 2 * (control & 0x03);
      data = (data >> shift) | (p << (8 - shift));
    }

    if (control & 0x40)
      data = (data >> 6) | ((data >> 2) & 0x0c) | ((data << 2) & 0x30) | (data << 6);

    if (control & 0x30) {
      intercept &= 0x0f;
      if ((old & 0xc0) && (data & 0xc0)) intercept |= 0x11;
      if ((old & 0x30) && (data & 0x30)) intercept |= 0x22;
      if ((old & 0x0c) && (data & 0x0c)) intercept |= 0x44;
      if ((old & 0x03) && (data & 0x03)) intercept |= 0x88;
      if (control & 0x10)
        data |= old;
      else
        data ^= old;
    }
    *out = data;
    return true;
  }
};

struct Pattern {
  uint16_t source = 0, dest = 0;
  uint8_t mode = 0, skip = 0, width = 0, height = 0;

  // Ports 0x78..0x7e (reg 0..6). Reg 6 runs the blit through bus.rd/bus.wr
  // (full program map, so dest < 0x4000 hits the funcgen). Returns CPU cycles
  // used (4 per byte), 0 for other regs.
  template <class Bus>
  int write(uint8_t reg, uint8_t d, Bus &bus) {
    switch (reg) {
    case 0: source = (source & 0xff00) | d; break;
    case 1: source = (source & 0x00ff) | (d << 8); break;
    case 2:
      mode = d & 0x3f;
      dest &= 0xff00;
      break;
    case 3: skip = d; break;
    case 4: dest = ((dest + skip) & 0xff) | (d << 8); break;
    case 5: width = d; break;
    case 6:
      height = d;
      return blit(bus);
    }
    return 0;
  }

private:
  void incSource(uint8_t curwidth, uint8_t *u13ff) {
    if (*u13ff && (mode & 0x04) != 0 && (curwidth != 0 || (mode & 0x08) == 0)) source++;
    if ((mode & 0x02) != 0) *u13ff ^= 1;
  }
  void incDest(uint8_t curwidth) {
    if (curwidth != 0) {
      if ((mode & 0x20) != 0)
        dest++;
      else
        dest--;
    }
  }

  template <class Bus>
  int blit(Bus &bus) {
    int cycles = 0;
    uint8_t u13ff = (mode & 0x02) == 0 ? 1 : 0;
    do {
      uint8_t curwidth = width;
      do {
        uint8_t data;
        if ((mode & 0x01) == 0) {
          data = (curwidth == 0 && (mode & 0x08) != 0) ? 0 : bus.rd(source);
          incSource(curwidth, &u13ff);
          bus.wr(dest, data);
          incDest(curwidth);
        } else {
          data = (curwidth == 0 && (mode & 0x08) != 0) ? 0 : bus.rd(dest);
          incDest(curwidth);
          bus.wr(source, data);
          incSource(curwidth, &u13ff);
        }
        cycles += 4;
      } while (curwidth-- != 0);

      const uint16_t carry = ((dest & 0xff) + skip) & 0x100;
      dest = (dest & 0xff00) | ((dest + skip) & 0xff);
      if ((mode & 0x10) == 0)
        dest += carry;
      else
        dest -= carry ^ 0x100;
    } while (height-- != 0);
    return cycles;
  }
};

} // namespace astrocade

#endif
