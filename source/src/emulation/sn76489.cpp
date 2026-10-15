#include "sn76489.h"

static const int SN_GAIN = 6;

Sn76489::Sn76489(int inc, uint8_t flags) : inc(inc), flags(flags) {}

// MAME sn76496.cpp write(): a latch byte (bit 7) selects the register and
// carries its low 4 bits, a data byte carries the high 6 bits of a tone
// period, or a whole volume / noise value
void Sn76489::write(uint8_t data) {
  if (data & LATCH) {
    latch = (data >> 4) & 0x07;
  }
  const int ch = latch >> 1;
  if (latch & 1) {
    setVolume(ch, data & 0x0f);
    return;
  }
  if (ch == NOISE) {
    noise_reg = data & 0x07;
    updateNoisePeriod();
    return;
  }
  if (data & LATCH) {
    period[ch] = (period[ch] & 0x3f0) | (data & 0x0f);
  } else {
    period[ch] = (period[ch] & 0x00f) | ((data & 0x3f) << 4);
  }
  if (ch == TONE2) {
    updateNoisePeriod();
  }
}

void Sn76489::setVolume(int ch, int vol) {
  volume[ch] = vol;
  if (!(flags & HOLD_SHORT_NOTES)) {
    return;
  }
  if (vol < min_volume[ch]) {
    min_volume[ch] = vol;
  }
  if (vol < OFF) {
    hold[ch] = HOLD_RENDERS;
  }
}

// noise rate N/512, N/1024, N/2048 or tone 2 output (shifts every 2 tone
// periods, MAME sn76496)
void Sn76489::updateNoisePeriod(void) {
  const int rate = noise_reg & 0x03;
  const int p = rate == FOLLOW_TONE2 ? (period[TONE2] >> 2) : (NOISE_BASE << rate);
  period[NOISE] = p > 0 ? p : 1;
}

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
      if (vol[c] >= OFF || p <= 0) {
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
      sample += toggle[c] * (OFF - vol[c]) * SN_GAIN;
    }
    out[i] = sample;
  }
}
