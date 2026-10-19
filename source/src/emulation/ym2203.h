#ifndef YM2203_H
#define YM2203_H

#include <stdint.h>
#include "ay8910.h"

class Ym2203Fm;

// Yamaha YM2203 port: address latch, the SSG (an AY-3-8910 core, registers
// 0-15) and the FM part (registers $20 and up, Ym2203Fm). The FM part is
// optional: boards that replace FM music (commando, gng: SID) pass none and
// their FM writes are dropped. FM is rendered by its owner (two chips batch
// into one accumulator, see _1943::renderFm).
class Ym2203 {
public:
  Ym2203(uint8_t ssg_inc, uint8_t ssg_gain, Ym2203Fm *fm = nullptr);

  // emulation core: the CPU's port writes
  void writeAddress(uint8_t addr);
  void writeData(uint8_t value);
  // chip reset: address latch and SSG registers cleared
  void reset(void);

  // audio core: the SSG part, at Ay8910 scale
  void renderSsg(int16_t *out, int n);

  template <int N>
  static void renderSsgBank(Ym2203 (&chips)[N], int16_t *out, int n) {
    chips[0].renderSsg(out, n);
    for (int c = 1; c < N; c++) {
      int16_t part[AUDIO_CHUNK];
      chips[c].renderSsg(part, n);
      audioAdd(out, part, n);
    }
  }

private:
  Ym2203Fm *fm;
  uint8_t addr = 0;
  uint8_t ssg_regs[Ay8910::REG_COUNT] = {};
  Ay8910 ssg;
};

#endif
