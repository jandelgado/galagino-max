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
  // chip reset: address latch, SSG registers and timers cleared
  void reset(void);

  // emulation core: timers. b0 timer A flag, b1 timer B flag, b7 busy
  // (never set: the busy window is shorter than a Z80 poll).
  uint8_t readStatus() const { return status; }
  // advance both timers by chip clock cycles
  void tick(uint32_t chip_cycles);
  // cycles until timer B expires, UINT32_MAX when stopped
  uint32_t cyclesToTimerB() const { return b_run ? b_left : UINT32_MAX; }

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

  // timers, plain members (emulation core only)
  uint16_t a_val = 0;   // 10 bit
  uint8_t b_val = 0;
  uint8_t pre = 6;      // prescaler 6, 3 or 2
  uint8_t mode = 0;     // reg 27
  uint8_t status = 0;
  bool a_run = false;
  bool b_run = false;
  uint32_t a_left = 0;
  uint32_t b_left = 0;

  // chip cycles, ymfm: 12 operators * pre * (1024 - NA) / 16 * (256 - NB)
  uint32_t periodA() const { return 12u * pre * (1024u - a_val); }
  uint32_t periodB() const { return 192u * pre * (256u - b_val); }
  void writeMode(uint8_t value);
};

#endif
