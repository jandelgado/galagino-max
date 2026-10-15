#ifndef POKEY_H
#define POKEY_H

#include "audio_mix.h"
#include <stdint.h>

// Atari POKEY audio part (port of MAME devices/sound/pokey.cpp): 4
// channels, AUDCTL clocks/joins/high-pass filters, poly4/5/9/17 noise,
// RANDOM. No pots, keyboard, serial or timer IRQs. Event driven: work per
// channel borrow, not per POKEY clock. POKEY clocks per 24 kHz sample set
// per instance in reset().
class Pokey {
public:
  // index into the regs array render() takes; 0..7 are AUDF1, AUDC1 .. AUDC4
  static const int REG_AUDCTL = 8;
  static const int REG_SKCTL = 9;
  static const int REG_COUNT = 10;
  // POKEY clocks per 24 kHz sample: 1.512 MHz (Centipede, Millipede) and
  // 1.7898 MHz (Tetris; 74.57 rounded, 0.6% sharp), 1.25 MHz (Crystal
  // Castles; 52.08 rounded). At most 128.
  static const int32_t CLOCKS_1512KHZ = 63;
  static const int32_t CLOCKS_1789KHZ = 75;
  static const int32_t CLOCKS_1250KHZ = 52;

  explicit Pokey(int32_t clocks_per_sample = CLOCKS_1512KHZ) { reset(clocks_per_sample); }
  void reset(int32_t clocks_per_sample = CLOCKS_1512KHZ);
  // n samples centered on 0, about +-480
  void render(const uint8_t *regs, int16_t *out, int n);

  // sum of N chips, chip c's registers at regs[c * REG_COUNT]; up to
  // +-(N * 240), not clipped
  template <int N>
  static void renderBank(Pokey (&chips)[N], const uint8_t *regs, int16_t *out, int n) {
    chips[0].render(regs, out, n);
    for (int c = 1; c < N; c++) {
      int16_t part[AUDIO_CHUNK];
      chips[c].render(regs + c * REG_COUNT, part, n);
      audioAdd(out, part, n);
    }
  }
  // RANDOM register value at POKEY clock count clock
  static uint8_t random(uint32_t clock, bool poly9);

private:
  struct Channel {
    int32_t counter; // clocks until next borrow, from current sample start
    uint8_t out, filter;
  };
  Channel ch[4];
  uint32_t i4, i5, i9;      // poly positions at current sample start
  uint32_t p17;             // poly17 position at next sample start
  uint32_t s17;             // poly17 window at p17, stale if !s17_valid
  bool s17_valid;
  int32_t cps;              // POKEY clocks per sample
  uint8_t cps4, cps5;       // cps mod LEN4, LEN5
  uint64_t w17[2];          // poly17 bits of current sample, bit t = clock t
  int32_t dc_q8, lp;        // DC blocker (Q8) and low pass state
  void event(int c, uint8_t audc, uint8_t audctl, int32_t t);
};

#endif
