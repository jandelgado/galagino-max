#include "pokey.h"
#include "pokey_poly.h"

static const uint32_t LEN4 = 15, LEN5 = 31, LEN9 = 511, LEN17 = 131071;
// RANDOM (poly17) byte = output bits i-9 .. i-2, see random()
static const uint32_t RANDOM17_LAG = 9;

// AUDCx
static const uint8_t NOTPOLY5 = 0x80, POLY4 = 0x40, PURE = 0x20, VOLUME_ONLY = 0x10, VOLUME_MASK = 0x0f;
// AUDCTL
static const uint8_t POLY9 = 0x80, CH1_HICLK = 0x40, CH3_HICLK = 0x20, CH12_JOINED = 0x10,
                     CH34_JOINED = 0x08, CH1_FILTER = 0x04, CH2_FILTER = 0x02, CLK_15KHZ = 0x01;
// SKCTL: both bits clear holds the chip in reset
static const uint8_t SK_RESET = 0x03;

// prescalers: 1.512 MHz / 28 = 54 kHz, / 114 = 13.3 kHz (MAME DIV_64, DIV_15)
static const int32_t DIV_64 = 28, DIV_15 = 114;
// extra clocks of a high clock channel, alone (AUDF+4) and joined (AUDF+7)
static const int32_t HICLK_EXTRA = 4, HICLK_JOINED_EXTRA = 7;

// output: 4 channels * volume 15 * 63 clocks -> 0..960
static const int32_t OUTPUT_SCALE = 16;
static const int32_t OUTPUT_MAX = 511;
// DC blocker time constant 2^9 samples (~21 ms)
static const int DC_SHIFT = 9;
// one-pole low pass ~4.8 kHz at 24 kHz (3.3k/0.01uF opamp stage), alpha 184/256
static const int32_t LOWPASS_ALPHA = 184;

static inline uint32_t wrap(uint32_t i, uint32_t len) { return i >= len ? i - len : i; }

// borrow counter after clocks more POKEY clocks without events
static int32_t skip_borrows(int32_t counter, int32_t period, int32_t clocks) {
  if (counter >= clocks) {
    return counter - clocks;
  }
  return (period - (clocks - counter) % period) % period;
}
static inline uint8_t bit17(uint32_t i) { return (pokey_poly17_bits[i >> 3] >> (i & 7)) & 1; }

void Pokey::reset() {
  for (int c = 0; c < 4; c++) {
    ch[c].counter = 0;
    ch[c].out = 0;
    ch[c].filter = c < 2 ? 1 : 0; // MAME device_reset
  }
  i4 = i5 = i9 = i17 = 0;
  dc_q8 = lp = 0;
}

uint8_t Pokey::random(uint32_t clock, bool poly9) {
  if (poly9) {
    return pokey_poly9[clock % LEN9] & 0xff;
  }
  // bits 8..16 of the 17 bit state only shift, so bit k (8..15) of the
  // state at i is output bit i-17+k
  const uint32_t i = clock % LEN17 + LEN17 - RANDOM17_LAG;
  uint8_t v = 0;
  for (int k = 0; k < 8; k++) {
    v |= bit17(wrap(i + k, LEN17)) << k;
  }
  return v;
}

// borrow of channel c at clock offset t (0..62) into the current sample
// (MAME process_channel)
void Pokey::event(int c, uint8_t audc, uint8_t audctl, int32_t t) {
  if (!(audc & NOTPOLY5) && !((pokey_poly5_bits >> ((i5 + t) % LEN5)) & 1)) {
    return;
  }
  Channel &h = ch[c];
  if (audc & PURE) {
    h.out ^= 1;
  } else if (audc & POLY4) {
    h.out = (pokey_poly4_bits >> ((i4 + t) % LEN4)) & 1;
  } else if (audctl & POLY9) {
    h.out = pokey_poly9[wrap(i9 + t, LEN9)] & 1;
  } else {
    h.out = bit17(wrap(i17 + t, LEN17));
  }
}

void Pokey::render(const uint8_t *regs, int16_t *out, int n) {
  const uint8_t audctl = regs[REG_AUDCTL];
  const bool running = (regs[REG_SKCTL] & SK_RESET) != 0;
  const int32_t base = (audctl & CLK_15KHZ) ? DIV_15 : DIV_64;

  int32_t period[4];
  for (int c = 0; c < 4; c++) {
    period[c] = (regs[2 * c] + 1) * base;
  }
  if (audctl & CH1_HICLK) {
    period[0] = regs[0] + HICLK_EXTRA;
  }
  if (audctl & CH3_HICLK) {
    period[2] = regs[4] + HICLK_EXTRA;
  }
  if (audctl & CH12_JOINED) {
    const int32_t f = regs[0] | (regs[2] << 8);
    period[1] = (audctl & CH1_HICLK) ? f + HICLK_JOINED_EXTRA : (f + 1) * base;
    period[0] = 0;
  }
  if (audctl & CH34_JOINED) {
    const int32_t f = regs[4] | (regs[6] << 8);
    period[3] = (audctl & CH3_HICLK) ? f + HICLK_JOINED_EXTRA : (f + 1) * base;
    period[2] = 0;
  }
  // a counter left from a longer old period restarts with the new one
  for (int c = 0; c < 4; c++) {
    if (ch[c].counter > period[c]) {
      ch[c].counter = period[c];
    }
  }

  // Borrows only change the channel output and, from ch3/ch4, the ch1/ch2
  // high-pass latch. A channel at volume 0 or volume only, clocking no
  // enabled filter, only advances its counter. Loses a muted pure tone's
  // phase. Idle Centipede parks ch3 at AUDF 0 high clock (4 clocks): ~16
  // borrows per sample otherwise.
  bool live[4];
  for (int c = 0; c < 4; c++) {
    const uint8_t audc = regs[2 * c + 1];
    live[c] = (audc & VOLUME_MASK) && !(audc & VOLUME_ONLY);
  }
  live[2] = live[2] || (audctl & CH1_FILTER);
  live[3] = live[3] || (audctl & CH2_FILTER);
  if (running) {
    for (int c = 0; c < 4; c++) {
      if (!live[c] && period[c]) {
        ch[c].counter = skip_borrows(ch[c].counter, period[c], n * CLOCKS_PER_SAMPLE);
      }
    }
    // unfiltered: the latch a borrow would set
    if (!live[2] && period[2]) {
      ch[0].filter = 1;
    }
    if (!live[3] && period[3]) {
      ch[1].filter = 1;
    }
  }

  static const int ORDER[4] = {2, 3, 0, 1}; // MAME step_one_clock order
  for (int s = 0; s < n; s++) {
    int32_t acc = 0; // sum of volume * clocks high within this sample
    for (int k = 0; k < 4; k++) {
      const int c = ORDER[k];
      Channel &h = ch[c];
      const uint8_t audc = regs[2 * c + 1];
      int32_t high = 0, pos = 0;
      uint8_t level = (audc & VOLUME_ONLY) ? 1 : (h.out ^ h.filter);
      if (running && period[c] && live[c]) {
        while (h.counter < CLOCKS_PER_SAMPLE) {
          if (level) {
            high += h.counter - pos;
          }
          pos = h.counter;
          event(c, audc, audctl, pos);

          // ch3/ch4 borrow clocks the ch1/ch2 high-pass flip-flop
          if (c == 2) {
            ch[0].filter = (audctl & CH1_FILTER) ? ch[0].out : 1;
          }
          if (c == 3) {
            ch[1].filter = (audctl & CH2_FILTER) ? ch[1].out : 1;
          }

          level = (audc & VOLUME_ONLY) ? 1 : (h.out ^ h.filter);
          h.counter += period[c];
        }
        h.counter -= CLOCKS_PER_SAMPLE;
      }
      if (level) {
        high += CLOCKS_PER_SAMPLE - pos;
      }
      acc += (audc & VOLUME_MASK) * high;
    }
    if (running) {
      i4 = (i4 + CLOCKS_PER_SAMPLE) % LEN4;
      i5 = (i5 + CLOCKS_PER_SAMPLE) % LEN5;
      i9 = wrap(i9 + CLOCKS_PER_SAMPLE, LEN9);
      i17 = wrap(i17 + CLOCKS_PER_SAMPLE, LEN17);
    }

    // remove DC (POKEY output is unipolar), then low pass
    const int32_t x = acc * OUTPUT_SCALE / CLOCKS_PER_SAMPLE;
    dc_q8 += ((x << 8) - dc_q8) >> DC_SHIFT;
    lp += ((x - (dc_q8 >> 8) - lp) * LOWPASS_ALPHA) >> 8;
    out[s] = lp > OUTPUT_MAX ? OUTPUT_MAX : (lp < -OUTPUT_MAX ? -OUTPUT_MAX : (int16_t)lp);
  }
}
