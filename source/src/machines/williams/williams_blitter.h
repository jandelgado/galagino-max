#ifndef WILLIAMS_BLITTER_H
#define WILLIAMS_BLITTER_H

#include <stdint.h>

// Williams Special Chip 1 (Robotron, Joust), MAME williamsblitter.cpp.
// Registers 0xca00-0xca07; writing control (0) runs the whole blit at once
// and returns the 6809 cycles the chip halted the CPU for.
// Bus: read() = CPU view (ROM overlay), readVram() = RAM below 0xc000,
// write() = CPU write path.
class Sc1Blitter {
public:
  void reset() { *this = Sc1Blitter(); }

  template <class Bus> uint32_t write(uint8_t reg, uint8_t val, Bus &bus) {
    switch (reg & 7) {
      case 0: control = val; return blit(bus);
      case 1: solid = val; break;
      case 2: src = (src & 0x00ff) | (val << 8); break;
      case 3: src = (src & 0xff00) | val; break;
      case 4: dst = (dst & 0x00ff) | (val << 8); break;
      case 5: dst = (dst & 0xff00) | val; break;
      case 6: width = val; break;
      default: height = val; break;
    }
    return 0;
  }

private:
  static const uint8_t SRC_STRIDE_256 = 0x01, DST_STRIDE_256 = 0x02, SLOW = 0x04;
  static const uint8_t FG_ONLY = 0x08, SOLID = 0x10, SHIFT = 0x20;
  static const uint8_t NO_ODD = 0x40, NO_EVEN = 0x80;
  static const uint8_t SIZE_XOR = 4;  // SC1 bug: width and height XOR 4

  static uint16_t nextRow(uint16_t start, int adv, bool stride256) {
    if (stride256) {
      return (start & 0xff00) | ((start + adv) & 0xff);  // X does not wrap
    }
    return start + adv;
  }

  template <class Bus> uint32_t blit(Bus &bus) {
    int w = width ^ SIZE_XOR, h = height ^ SIZE_XOR;
    if (w == 0) {
      w = 1;
    }
    if (h == 0) {
      h = 1;
    }
    const bool sStride = control & SRC_STRIDE_256, dStride = control & DST_STRIDE_256;
    const int sxadv = sStride ? 0x100 : 1, syadv = sStride ? 1 : w;
    const int dxadv = dStride ? 0x100 : 1, dyadv = dStride ? 1 : w;

    uint16_t sstart = src, dstart = dst;
    uint32_t shifted = 0;
    for (int y = 0; y < h; y++) {
      uint16_t s = sstart, d = dstart;
      for (int x = 0; x < w; x++) {
        const uint8_t raw = bus.read(s);
        if (control & SHIFT) {
          shifted = (shifted << 8) | raw;
          pixel(bus, d, (shifted >> 4) & 0xff);
        } else {
          pixel(bus, d, raw);
        }
        s += sxadv;
        d += dxadv;
      }
      dstart = nextRow(dstart, dyadv, dStride);
      sstart = nextRow(sstart, syadv, sStride);
    }

    // bus time at 4 MHz, MAME estimate
    const uint32_t accesses = 2u * w * h;
    const uint32_t clocks = 4 + ((control & SLOW) ? 4 * (accesses + 2) : 2 * (accesses + 3));
    return (clocks + 3) / 4;
  }

  // Even pixel = high nibble. A 0 source nibble in foreground-only mode is
  // transparent; NO_EVEN/NO_ODD skip a nibble.
  template <class Bus> void pixel(Bus &bus, uint16_t addr, uint8_t data) {
    uint8_t keep = 0xff;
    const bool fg = control & FG_ONLY;
    if (fg && !(data & 0xf0)) {
      if (control & NO_EVEN) {
        keep &= 0x0f;
      }
    } else if (!(control & NO_EVEN)) {
      keep &= 0x0f;
    }
    if (fg && !(data & 0x0f)) {
      if (control & NO_ODD) {
        keep &= 0xf0;
      }
    } else if (!(control & NO_ODD)) {
      keep &= 0xf0;
    }
    const uint8_t value = (control & SOLID) ? solid : data;
    bus.write(addr, (bus.readVram(addr) & keep) | (value & ~keep));
  }

  uint8_t control = 0, solid = 0, width = 0, height = 0;
  uint16_t src = 0, dst = 0;
};

#endif
