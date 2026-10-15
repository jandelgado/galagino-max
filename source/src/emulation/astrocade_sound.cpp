#include "astrocade_sound.h"

// Q16 chip clocks per 24 kHz output sample: 14318181 / 8 / 24000
static const uint32_t STEP_Q16 = 4887244;

static inline uint8_t reverse8(uint8_t x) {
  x = (uint8_t)((x >> 4) | (x << 4));
  x = (uint8_t)(((x & 0xcc) >> 2) | ((x & 0x33) << 2));
  return (uint8_t)(((x & 0xaa) >> 1) | ((x & 0x55) << 1));
}

void AstrocadeSound::reset() {
  for (int i = 0; i < 8; i++) {
    regs[i] = 0;
  }
  master = noiseClock = 0;
  for (int i = 0; i < TONES; i++) {
    tone[i] = Tone();
  }
  noise = vibrato = 0;
  frac = 0;
}

// MAME master oscillator reload value: ~reg0 plus vibrato or noise. Only
// changes at noise clock boundaries (vibrato counter, LFSR) or on a write.
uint8_t AstrocadeSound::reload() const {
  uint8_t m = (uint8_t)~regs[0];
  if ((regs[5] & 0x10) == 0) {
    // mux: vibrato; speed picks one of the top bits of the 13 bit clock
    if (!((vibrato >> (regs[4] >> 6)) & 0x0200)) {
      m = (uint8_t)(m + (regs[4] & 0x3f));
    }
    return m;
  }

  // mux: noise; top 8 LFSR bits (reversed) ANDed with noise volume
  return (uint8_t)(m + (reverse8((noise >> 7) & 0xff) & regs[7]));
}

// One master overflow: the tone counter steps, flips when it wraps.
void AstrocadeSound::toneStep(Tone &t, uint16_t every) {
  if (--t.left == 0) {
    t.on = !t.on;
    t.left = every;
  }
}

// Advances a tone over a span, returns its on-clocks in [0, len). Flips
// are evenly spaced (every * period clocks), so the integral is closed
// form instead of one pass per overflow.
int32_t AstrocadeSound::toneSpan(Tone &t, uint16_t every, const Overflows &o, int32_t len) {
  if (o.count < t.left) {
    t.left -= o.count;
    return t.on ? len : 0;
  }

  // flips at first + j * gap, j < flips; segment j after flip j
  const int32_t first = o.first + (t.left - 1) * o.period;
  const int32_t gap = every * o.period;
  const int32_t flips = 1 + (len - 1 - first) / gap;
  const int32_t full = flips - 1;           // whole segments 0 .. flips - 2
  const int32_t even = flips / 2;           // of those: state !on
  const int32_t odd = full - even;          // state on
  const bool last = (full & 1) ? t.on : !t.on;
  const int32_t tail = len - first - full * gap;

  int32_t onClocks = t.on ? first + odd * gap : even * gap;
  if (last) {
    onClocks += tail;
  }

  // overflows after the last flip
  const int32_t after = o.count - (t.left + full * every);
  t.on = last;
  t.left = (uint16_t)(every - after);
  return onClocks;
}

// len chip clocks without a noise boundary inside; returns level * clocks.
// A master overflow exactly at the span end is left to the caller (it
// reloads after the noise clock, as MAME does).
int32_t AstrocadeSound::span(int32_t len, bool &overflowAtEnd) {
  const uint8_t r5 = regs[5], r6 = regs[6];
  Overflows o;
  o.first = 256 - master;
  o.period = 256 - reload();
  o.count = o.first < len ? 1 + (len - 1 - o.first) / o.period : 0;

  int32_t sum = ((r5 & 0x20) && (noise & 0x4000)) ? (regs[7] >> 4) * len : 0;
  const int32_t vol[TONES] = {r6 & 0x0f, r6 >> 4, r5 & 0x0f};
  for (int i = 0; i < TONES; i++) {
    sum += vol[i] * toneSpan(tone[i], regs[1 + i] + 1, o, len);
  }

  // master counter at the span end; 256 = overflow right at the end
  int32_t end = master + len;
  if (o.count) {
    end = 256 - o.period + len - (o.first + (o.count - 1) * o.period);
  }
  overflowAtEnd = end == 256;
  master = (uint8_t)end;
  return sum;
}

// MAME sound_stream_update, one chip clock per stream sample. Spans end at
// noise clock boundaries (every 64 clocks) and output sample edges: at
// most 3 per sample, whatever the master period.
void AstrocadeSound::render(int32_t *acc, int n, int gain) {
  for (int i = 0; i < n; i++) {
    frac += STEP_Q16;
    int32_t left = (int32_t)(frac >> 16);
    frac &= 0xffff;
    const int32_t clocks = left;
    int32_t sum = 0;

    while (left > 0) {
      int32_t len = 64 - noiseClock;
      if (len > left) {
        len = left;
      }
      bool overflow;
      sum += span(len, overflow);
      left -= len;

      // 2 bit counter -> 4 bit counter -> LFSR (15 bit, XNOR of the top two)
      noiseClock = (uint8_t)(noiseClock + len);
      if (noiseClock == 64) {
        noise = noiseNext(noise);
        noiseClock = 0;
        vibrato++;
      }

      if (overflow) {
        master = reload();
        for (int t = 0; t < TONES; t++) {
          toneStep(tone[t], regs[1 + t] + 1);
        }
      }
    }
    acc[i] += sum * gain / clocks;
  }
}
