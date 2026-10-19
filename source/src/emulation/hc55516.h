#ifndef HC55516_H
#define HC55516_H

#include <stdint.h>

// Harris HC-55516 CVSD decoder, software clocked (Sinistar speech), after
// MAME devices/sound/hc55516.cpp: digit() latches the data bit, every
// clock() edge runs one integrator step; the rising edge shifts the bit in
// and adapts the syllabic filter. /FZ is never asserted on these boards.
class Hc55516 {
public:
  void reset() { *this = Hc55516(); }
  void digit(bool d) { bit = d; }

  void clock(bool level) {
    if (level == lastClock) {
      return;
    }
    lastClock = level;
    step(level);
  }

  // -32768..32767 (10 bit integrator, MAME scaling)
  int16_t output() const { return sample; }

private:
  static const uint8_t SHIFT_MASK = 0x07;
  static const int32_t SYL_MASK = 0xfc0, SYL_SHIFT = 6, SYL_ADD = 0xfc1, INT_SHIFT = 4;
  static const int32_t FREEZE = 0x180;

  static int32_t sext10(int32_t v) { return ((v & 0x3ff) ^ 0x200) - 0x200; }

  void step(bool rising) {
    const bool frozen = (integ >= FREEZE && !bit) || (integ <= -FREEZE && bit);
    int32_t sum;
    if (rising) {
      shift = (uint8_t)((shift << 1) | (bit ? 1 : 0));
      const uint8_t run = shift & SHIFT_MASK;
      if (!frozen) {
        syl += ((~syl) & SYL_MASK) >> SYL_SHIFT;
        if (run != 0 && run != SHIFT_MASK) {
          syl += SYL_ADD;  // no coincidence: decay
        }
      }
      syl &= 0xfff;
      sum = sext10(((~integ) >> INT_SHIFT) + 1);
    } else {
      const int32_t s = (syl >> 6) > 2 ? (syl >> 6) : 2;
      sum = (shift & 1) ? sext10(~s + 1) : sext10(s);
    }
    if (!frozen) {
      integ = sext10(integ + sum);
    }
    sample = (int16_t)((integ << 6) | (((integ & 0x3ff) ^ 0x200) >> 4));
  }

  int32_t syl = 0x3f, integ = 0;
  uint8_t shift = 0;
  bool bit = false, lastClock = false;
  int16_t sample = 0;
};

#endif
