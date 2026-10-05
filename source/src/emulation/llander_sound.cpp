#include "llander_sound.h"

static const int32_t SAMPLE_MAX = 511;

// MAME relative gains: thrust data 0-7 times 600/7*7.6, the explosion adds
// the unfiltered thrust noise times 1000/600
static const float GAIN_THRUST = 600.0f / 7 * 7.6f;
static const float GAIN_EXPLODE = 1000.0f / 600.0f;

// MAME units to +-511: full thrust plus explosion peaks at the limit with
// about 1 % clipped samples; tune on the device
static const float MIX_GAIN = 0.35f;

// The 3/6 kHz beeps are 9.2 p-p in MAME's table, about 45 dB under full
// thrust: +-2 here, inaudible. They get a fixed level in output units,
// about full thrust loudness.
static const int16_t TONE_LEVEL = 30;

// 24 kHz / 3 kHz = 8 samples per period, 6 kHz = 4
static const uint8_t TONE_PERIOD = 8;

void LlanderSound::reset() {
  noise.reset();
  noise_rc.set(70.8f); // RC 2247 ohm, 1 uF
  thrust_bp.set(89.5f, 1.0f / 7.6f);
  out_lp.set(560.0f);
  tone_count = 0;
}

void LlanderSound::render(const uint8_t *regs, int16_t *out, int n) {
  const uint8_t ctrl = regs[LL_SND_CTRL];
  const float volume = (ctrl & LL_THRUST_MASK) * GAIN_THRUST;
  for (int i = 0; i < n; i++) {
    const float thrust = noise_rc.step(noise.step(regs[LL_SND_NOISE_RESET])) * volume;
    float rumble = thrust_bp.step(thrust);
    if (ctrl & LL_EXPLODE) {
      rumble += thrust * GAIN_EXPLODE;
    }
    int32_t s = (int32_t)(out_lp.step(rumble) * MIX_GAIN);

    const bool high_3k = tone_count < TONE_PERIOD / 2;
    const bool high_6k = tone_count % (TONE_PERIOD / 2) < TONE_PERIOD / 4;
    if (ctrl & LL_TONE_3K) {
      s += high_3k ? TONE_LEVEL : -TONE_LEVEL;
    }
    if (ctrl & LL_TONE_6K) {
      s += high_6k ? TONE_LEVEL : -TONE_LEVEL;
    }
    tone_count = (tone_count + 1) % TONE_PERIOD;

    out[i] = s > SAMPLE_MAX ? SAMPLE_MAX : (s < -SAMPLE_MAX ? -SAMPLE_MAX : s);
  }
}
