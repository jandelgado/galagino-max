#include "sn76489.h"

static const int SN_GAIN = 6;
static const int SN_OFF = 15;

Sn76489::Sn76489(int inc) : inc(inc) {}

void Sn76489::render(int16_t *out, int n) {
  int vol[CHANNELS];
  for (int c = 0; c < CHANNELS; c++) {
    if (hold[c] > 0) {
      vol[c] = min_volume[c];
      hold[c]--;
      if (hold[c] == 0) {
        min_volume[c] = volume[c];
      }
    } else {
      vol[c] = volume[c];
      min_volume[c] = volume[c];
    }
  }

  for (int i = 0; i < n; i++) {
    int16_t sample = 0;
    for (int c = 0; c < CHANNELS; c++) {
      const int p = period[c];
      if (vol[c] >= SN_OFF || p <= 0) {
        continue;
      }
      counter[c] -= inc;
      while (counter[c] <= 0) {
        if (c == NOISE) {
          const uint32_t feedback = (noise_lfsr & 0x01) ^ ((noise_lfsr & 0x02) >> 1);
          noise_lfsr = (noise_lfsr >> 1) | (feedback << 14);
          toggle[c] = (noise_lfsr & 0x01) ? 1 : -1;
          counter[c] += (p << 3);  // noise slowed down
        } else {
          counter[c] += p;
          toggle[c] = -toggle[c];
        }
      }
      sample += toggle[c] * (SN_OFF - vol[c]) * SN_GAIN;
    }
    out[i] = sample;
  }
}
