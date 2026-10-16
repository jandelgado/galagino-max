#include "ay8910.h"

Ay8910::Ay8910(uint8_t inc, uint8_t gain, uint8_t flags)
    : inc(inc), gain(gain), flags(flags), chanGain{gain, gain, gain} {}

void Ay8910::render(const uint8_t *regs, int16_t *out, int n) {
  for (int c = 0; c < TONES; c++) {
    period[c] = regs[2 * c] + 256 * (regs[2 * c + 1] & 0x0f);  // 12 bit
    enable[c] = (((regs[7] >> c) & 1) | ((regs[7] >> (c + 2)) & 2)) ^ 3;
    if (flags & NO_NOISE) {
      enable[c] &= 1;
    }
    volume[c] = regs[8 + c];
    envelope[c] = ((volume[c] & 0x10) == 0x10) && !(flags & NO_PERIOD0_ENV);
  }
  period[NOISE] = regs[6] & 0x1f;  // 5 bit
  env_period = regs[11] + 256 * regs[12];

  // a new shape (R13) restarts the envelope
  const uint8_t shape = regs[13];
  if (shape != env_shape) {
    env_shape = shape & 0x0f;
    env_counter = 0;
    env_step = (env_shape < 4 || (env_shape >= 8 && env_shape < 12)) ? 0 : 15;
    env_holding = 0;
  }

  for (int i = 0; i < n; i++) {
    int16_t value = 0;
    stepEnvelope();

    if (period[NOISE]) {
      cnt[NOISE] += inc;
      if (cnt[NOISE] > period[NOISE]) {
        cnt[NOISE] -= period[NOISE];
        noise_rng ^= (((noise_rng & 1) ^ ((noise_rng >> 3) & 1)) << 17);
        noise_rng >>= 1;
      }
    }

    for (int c = 0; c < TONES; c++) {
      if (!((period[c] || envelope[c]) && volume[c] && enable[c])) {
        continue;
      }
      // bit 4 of the volume register selects the envelope
      const int level = (volume[c] & 0x10) ? env_step : (volume[c] & 0x0f);
      if (level > 0) {
        int16_t bit = 1;
        if (enable[c] & 1) {
          bit &= (toggle[c] > 0) ? 1 : 0;
        }
        if (enable[c] & 2) {
          bit &= (noise_rng & 1) ? 1 : 0;
        }
        if (bit == 0) {
          bit = -1;
        }
        value += chanGain[c] * bit * level;
      }
      cnt[c] += inc;
      if (cnt[c] > period[c]) {
        cnt[c] -= period[c];
        toggle[c] = -toggle[c];
      }
    }
    out[i] = value;
  }
}

void Ay8910::stepEnvelope(void) {
  if (env_holding || env_period <= 0) {
    return;
  }
  env_counter += inc;
  if (env_counter < env_period) {
    return;
  }
  env_counter -= env_period;
  if (env_shape < 8) {  // attack: 0 up to 15
    env_step++;
    if (env_step > 15) {
      env_step = (env_shape & 1) ? 0 : 15;  // alternate: restart
      if (env_shape & 2) {
        env_holding = 1;
      }
    }
    return;
  }
  env_step--;  // decay: 15 down to 0
  if (env_step < 0) {
    env_step = (env_shape & 1) ? 15 : 0;
    if (env_shape & 2) {
      env_holding = 1;
    }
  }
}
