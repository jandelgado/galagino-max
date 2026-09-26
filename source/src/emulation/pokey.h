#ifndef POKEY_H
#define POKEY_H

#include <stdint.h>

// Atari POKEY audio part (port of MAME devices/sound/pokey.cpp): 4
// channels, AUDCTL clocks/joins/high-pass filters, poly4/5/9/17 noise,
// RANDOM. No pots, keyboard, serial or timer IRQs. Event driven: work per
// channel borrow, not per POKEY clock. Clock fixed at 63 POKEY clocks per
// 24 kHz sample (1.512 MHz, Centipede/Millipede).
class Pokey {
public:
  // index into the regs array render() takes; 0..7 are AUDF1, AUDC1 .. AUDC4
  static const int REG_AUDCTL = 8;
  static const int REG_SKCTL = 9;
  static const int REG_COUNT = 10;
  static const int32_t CLOCKS_PER_SAMPLE = 63;

  void reset();
  // n samples centered on 0, about +-480
  void render(const uint8_t *regs, int16_t *out, int n);
  // RANDOM register value at POKEY clock count clock
  static uint8_t random(uint32_t clock, bool poly9);

private:
  struct Channel {
    int32_t counter; // clocks until next borrow, from current sample start
    uint8_t out, filter;
  };
  Channel ch[4];
  uint32_t i4, i5, i9;      // poly positions at current sample start
  uint32_t s17;             // poly17 window at next sample start
  uint64_t w17;             // poly17 bits of current sample, bit t = clock t
  int32_t dc_q8, lp;        // DC blocker (Q8) and low pass state
  void event(int c, uint8_t audc, uint8_t audctl, int32_t t);
};

#endif
