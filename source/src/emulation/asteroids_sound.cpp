#include "asteroids_sound.h"
#include <math.h>

static const float SAMPLE_RATE = 24000.0f;
static const float DT = 1.0f / SAMPLE_RATE;
static const int32_t SAMPLE_MAX = 511;

// MAME relative gains (asteroid_discrete gain table), peak to peak
static const float GAIN_THUMP = 131.6f;
static const float GAIN_SAUCER = 76.1f;
static const float GAIN_LIFE = 100.0f;
static const float GAIN_EXPLODE = 1000.0f;
static const float GAIN_THRUST = 600.0f * 7.6f;

// MAME units to +-511: at 1.0 the loudest sound (explosion, volume 15)
// peaks at about 465
static const float MIX_GAIN = 1.0f;

// noise LFSR clock 12 kHz = every 2nd sample
static const uint8_t NOISE_DIV = 2;

// Thump 555 VCO, high time in samples per DAC value: R44-R47 ladder into a
// 2N3906 current source (22k) charging 0.22u from 1/3 to 2/3 of 5 V;
// discharge through 18k (0.693 * 18k * 0.22u = 66 samples). 132 .. 55 Hz.
static const uint16_t THUMP_HIGH[16] = {116, 121, 127, 132, 142, 148, 158, 166,
                                        188, 201, 218, 235, 265, 290, 328, 367};
static const uint16_t THUMP_LOW = 66;
static const uint8_t THUMP_ENABLE = 0x10;

// explosion: pitch divider of the 12 kHz noise clock, register bits 6-7
static const uint8_t EXPLODE_DIV[4] = {12, 6, 3, 5};

// life: 3 kHz square, 8 samples per period
static const uint8_t LIFE_PERIOD = 8;

void AsteroidsSound::LowPass::set(float fc) {
  a = 1.0f - expf(-2.0f * (float)M_PI * fc * DT);
  y = 0;
}

void AsteroidsSound::BandPass::set(float fc, float damp) {
  const float two_over_t = 2.0f * SAMPLE_RATE;
  const float wc = SAMPLE_RATE * 2.0f * tanf((float)M_PI * fc / SAMPLE_RATE);
  const float den = two_over_t * two_over_t + damp * wc * two_over_t + wc * wc;
  a1 = 2.0f * (-two_over_t * two_over_t + wc * wc) / den;
  a2 = (two_over_t * two_over_t - damp * wc * two_over_t + wc * wc) / den;
  b0 = damp * wc * two_over_t / den;
  x1 = x2 = y1 = y2 = 0;
}

float AsteroidsSound::BandPass::step(float x) {
  const float y = -a1 * y1 - a2 * y2 + b0 * (x - x2);
  x2 = x1;
  x1 = x;
  y2 = y1;
  y1 = y;
  return y;
}

// MAME: DISCRETE_RAMP (frequency falls linearly over 0.28 s), RCDISC
// (amplitude decays from amp to 0 with tau, plus 7 floor), SQUAREWAVE with
// duty 67 % + 4500 / f; at duty >= 100 % the output is DC, i.e. silent.
void AsteroidsSound::Fire::set(float f_start_, float f_end_, float amp_, float tau) {
  static const float RAMP_TIME = 0.28f;
  f_start = f_start_;
  f_end = f_end_;
  f_step = (f_start - f_end) * DT / RAMP_TIME;
  amp = amp_;
  decay = expf(-DT / tau);
  f = env = phase = 0;
}

float AsteroidsSound::Fire::step(bool on) {
  static const float AMP_FLOOR = 7.0f;
  if (!on) {
    f = 0;
    return 0;
  }
  if (f == 0) { // rising enable restarts ramp and decay
    f = f_start;
    env = amp;
    phase = 0;
  }

  const float a = env + AMP_FLOOR;
  const float duty = 0.67f + 45.0f / f;
  env *= decay;
  f = f - f_step > f_end ? f - f_step : f_end;
  if (duty >= 1.0f) {
    return 0;
  }

  phase += f * DT;
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  return phase > 1.0f - duty ? a / 2 : -a / 2;
}

void AsteroidsSound::reset() {
  lfsr = 0;
  noise_reset = 0;
  noise_div = 0;
  noise = 0;
  thump_count = 0;
  thump_high = false;
  lfo_phase = saucer_phase = 0;
  ship_fire.set(820.0f, 110.0f, 46.0f, 0.081f);  // R 2.7k * 3, C 10u
  saucer_fire.set(830.0f, 630.0f, 42.5f, 0.3f);   // R 10k * 3, C 10u
  explode_count = 0;
  explode_hold = 0;
  life_count = 0;
  thump_lp.set(482.0f);   // RC 3.3k, 0.1u
  thrust_lp1.set(72.3f);  // RC 2200, 1u
  thrust_lp2.set(160.0f); // active low pass
  explode_lp.set(52.3f);  // RC 3042, 1u
  thrust_bp.set(89.5f, 1.0f / 7.6f);
}

// 16 bit LFSR, XNOR of bits 6 and 14 shifted in (MAME asteroid_lfsr),
// +-0.5. Returns the new value on a 12 kHz tick, else the held one.
float AsteroidsSound::noise_step(uint8_t reset_count) {
  if (reset_count != noise_reset) {
    noise_reset = reset_count;
    lfsr = 0;
  }
  if (++noise_div < NOISE_DIV) {
    return noise;
  }

  noise_div = 0;
  const uint16_t fb = !(((lfsr >> 6) ^ (lfsr >> 14)) & 1);
  lfsr = (lfsr << 1) | fb;
  noise = fb ? 0.5f : -0.5f;
  return noise;
}

float AsteroidsSound::thump_step(uint8_t reg) {
  if (!(reg & THUMP_ENABLE)) {
    thump_count = 0;
    thump_high = false;
    return thump_lp.step(0);
  }

  const uint16_t len = thump_high ? THUMP_HIGH[reg & 0x0f] : THUMP_LOW;
  if (++thump_count >= len) {
    thump_count = 0;
    thump_high = !thump_high;
  }
  return thump_lp.step(thump_high ? GAIN_THUMP / 2 : -GAIN_THUMP / 2);
}

// Triangle at 750 Hz (large saucer 500 Hz) swept up by 0..920 Hz with a
// triangle LFO at 8.25 Hz (large 5.75 Hz). The LFO runs even when silent.
float AsteroidsSound::saucer_step(uint8_t latch) {
  const bool large = latch & AST_LATCH_SAUCER_LARGE;
  lfo_phase += (large ? 5.75f : 8.25f) * DT;
  if (lfo_phase >= 1.0f) {
    lfo_phase -= 1.0f;
  }
  if (!(latch & AST_LATCH_SAUCER)) {
    return 0;
  }

  const float lfo = lfo_phase < 0.5f ? lfo_phase * 2 : 2 - lfo_phase * 2;
  saucer_phase += ((large ? 500.0f : 750.0f) + 920.0f * lfo) * DT;
  if (saucer_phase >= 1.0f) {
    saucer_phase -= 1.0f;
  }
  const float tri = saucer_phase < 0.5f ? saucer_phase * 4 - 1 : 3 - saucer_phase * 4;
  return tri * GAIN_SAUCER / 2;
}

// Noise sampled at 12 kHz / divider, scaled by the 4 bit volume.
float AsteroidsSound::explode_step(uint8_t reg, float n) {
  if (noise_div == 0 && ++explode_count >= EXPLODE_DIV[reg >> 6]) {
    explode_count = 0;
    explode_hold = n;
  }
  const float volume = ((reg >> 2) & 0x0f) * (GAIN_EXPLODE / 15.0f);
  return explode_lp.step(explode_hold * volume);
}

void AsteroidsSound::render(const uint8_t *regs, int16_t *out, int n) {
  const uint8_t latch = regs[AST_SND_LATCH];
  for (int i = 0; i < n; i++) {
    const float nz = noise_step(regs[AST_SND_NOISE_RESET]);
    float v = thump_step(regs[AST_SND_THUMP]);
    v += saucer_step(latch);
    v += ship_fire.step(latch & AST_LATCH_SHIP_FIRE);
    v += saucer_fire.step(latch & AST_LATCH_SAUCER_FIRE);

    const float thrust = thrust_lp1.step(nz * GAIN_THRUST) * ((latch & AST_LATCH_THRUST) ? 1 : 0);
    v += thrust_lp2.step(thrust_bp.step(thrust));
    v += explode_step(regs[AST_SND_EXPLODE], nz);

    if (latch & AST_LATCH_LIFE) {
      v += life_count < LIFE_PERIOD / 2 ? GAIN_LIFE / 2 : -GAIN_LIFE / 2;
    }
    life_count = (life_count + 1) % LIFE_PERIOD;

    int32_t s = (int32_t)(v * MIX_GAIN);
    out[i] = s > SAMPLE_MAX ? SAMPLE_MAX : (s < -SAMPLE_MAX ? -SAMPLE_MAX : s);
  }
}
