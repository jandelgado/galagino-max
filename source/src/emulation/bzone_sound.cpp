#include "bzone_sound.h"
#include <math.h>

using discrete::DT;

static const int32_t SAMPLE_MAX = 511;

// MAME DISCRETE_OUTPUT(NODE_280, 48000): 32768 / 48000 V is full scale.
// MAME's level is quiet (idle engine peaks at a third of full scale);
// boosted so the engine nearly fills the range and explosions clip, like
// MAME's BZ_FINAL_GAIN note on real cabinets.
static const float LOUDNESS = 2.5f;
static const float OUT_GAIN = 48000.0f / 32768.0f * SAMPLE_MAX * LOUDNESS;

static const float VCC = 5.0f;
static const float TTL_HIGH = 3.4f;
static const float R_4066_ON = 270.0f;
static const float OPAMP_MAX = 22.0f - 1.5f; // B+ minus the rail offset

// POKEY: one channel at volume 15 swings 4.1 V in MAME's RC output model
// and 240 units in Pokey::render
static const float POKEY_VOLTS = 4.1f / 240.0f;

// final resistor mixer (Millman): shell R25, explosion R28, engine
// R26 + R20/4, POKEY R27, R29 to ground
static const float R_SHELL = 100e3f, R_EXPLO = 100e3f, R_ENGINE = 33e3f + 33e3f / 4;
static const float R_POKEY = 330e3f, R_GROUND = 22e3f;

static const uint8_t NOISE_DIV = 4; // 24 kHz / 6 kHz

// 555 VCO: R10, R11 pot at MAME's 40 % (75k..10k), C11
static const float R555_A = 100e3f, R555_B = 49e3f, C555 = 0.015e-6f;
static const float CV_MIN = 0.25f; // MAME: below this the 555 stops

static float rc_exp(float rc) { return 1.0f - expf(-DT / rc); }
static float parallel(float a, float b) { return a * b / (a + b); }

void BzoneSound::RcCircuit::set(float r, float c) {
  vc = 0;
  drop = (R_4066_ON + r) / (2 * R_4066_ON + r);
  exp1 = rc_exp(R_4066_ON * c);
  exp2 = rc_exp((R_4066_ON + r) * c);
  exp12 = rc_exp(parallel(R_4066_ON, R_4066_ON + r) * c);
}

float BzoneSound::RcCircuit::step(bool in0, bool in1) {
  if (!in0) {
    if (!in1) {
      return 0; // cap floats
    }
    vc -= vc * exp2;
    return vc * drop;
  }
  if (!in1) {
    vc += (VCC - vc) * exp1;
    return 0;
  }
  vc += (VCC * drop - vc) * exp12;
  return vc * drop;
}

// r1, r2: gain resistors (the 4066 shorts r1 when loud), r5: feedback,
// c: feedback cap; the input divider is R14/R15 = R17/R16 = 22k/1k
void BzoneSound::Filter::set(float r1, float r2, float r5, float c) {
  static const float IN_DIVIDER = 1e3f / (22e3f + 1e3f);
  y = 0;
  a = rc_exp(r5 * c);
  gain[0] = (r5 / (r1 + r2) + 1) * IN_DIVIDER;
  gain[1] = (r5 / (parallel(R_4066_ON, r1) + r2) + 1) * IN_DIVIDER;
}

float BzoneSound::Filter::step(float in, bool loud) {
  float v = in * gain[loud];
  v = v > OPAMP_MAX ? OPAMP_MAX : v;
  return y += (v - y) * a;
}

void BzoneSound::reset() {
  sr = 0;
  noise_div = 0;
  noise = n31 = n33 = n34 = false;

  shell_rc.set(22e3f + 1e3f, 4.7e-6f);  // R14 + R15, C9
  explo_rc.set(22e3f + 1e3f, 10e-6f);   // R17 + R16, C14
  shell_f.set(33e3f, 10e3f, 330e3f, 0.0047e-6f); // R12, R13, R32, C21
  explo_f.set(33e3f, 10e3f, 330e3f, 0.0047e-6f); // R19, R18, R33, C22

  vcv = 0;
  rev_fast = rc_exp(parallel(100e3f, 22e3f) * 10e-6f); // R8 || R9, C13
  rev_slow = rc_exp(100e3f * 10e-6f);                  // R8, C13
  t555 = t_high = 0;
  t_low = R555_B * C555 * logf(2.0f);
  high555 = true;
  f4 = f5 = 0;
  engine_a = rc_exp(parallel(33e3f, 33e3f) / 2 * 0.47e-6f); // 4 x R20.., C29
  engine_y = 0;

  dc_a = rc_exp(100e3f * 0.1e-6f); // MAME: 100k amp input, C20
  dc_cap = 0;
}

// MAME bzone_lfsr: 16 bit shift register, feedback !(bit 3 ^ bit 14) kept
// in bit 16, output bit 15; n31 and n34 divide noise edges by 2
void BzoneSound::noise_clock() {
  const uint32_t fb = ((sr >> 16) & 1) ^ 1;
  sr = ((sr << 1) & 0xffff) | fb;
  sr |= (((sr >> 3) ^ (sr >> 14)) & 1) << 16;

  const bool out = sr & 0x8000;
  if (out && !noise) {
    n31 = !n31;
  }
  noise = out;

  const bool nand = (sr & 0x7800) != 0x7800; // bits 11-14
  if (nand && !n33) {
    n34 = !n34;
  }
  n33 = nand;
}

// 555 VCO steered by the rev voltage, divided by two counters (F4: 4..15,
// F5: 6..15, held at 0 while the motor is off) whose QD and ripple
// outputs mix into the engine rumble
float BzoneSound::engine_step(uint8_t latch) {
  const float target = (latch & BZ_ENGINE_REV) ? 2.5f : 4.123f;
  const float diff = target - vcv;
  vcv += diff * (diff > 0.5f ? rev_fast : rev_slow);

  const bool motor = latch & BZ_MOTOR_EN;
  if (!motor) {
    f4 = f5 = 0;
  }

  if (vcv >= CV_MIN) {
    t555 += DT;
    if (high555 && t555 >= t_high) {
      t555 -= t_high;
      high555 = false;
    } else if (!high555 && t555 >= t_low) {
      t555 -= t_low;
      high555 = true;
      t_high = (R555_A + R555_B) * C555 * logf((VCC - vcv / 2) / (VCC - vcv));
      if (motor) {
        f4 = f4 >= 15 ? 4 : f4 + 1;
        f5 = f5 >= 15 ? 6 : f5 + 1;
      }
    }
  }

  const int high = (f4 > 7) + (f4 == 15) + (f5 > 7) + (f5 == 15);
  return engine_y += (high * TTL_HIGH / 4 - engine_y) * engine_a;
}

void BzoneSound::render(uint8_t latch, const int16_t *pokey, int16_t *out, int n) {
  static const float G_TOTAL =
      1 / R_SHELL + 1 / R_EXPLO + 1 / R_ENGINE + 1 / R_POKEY + 1 / R_GROUND;
  const bool loud = latch & BZ_EXPLO_LOUD; // MAME: also the shell's loudness

  for (int i = 0; i < n; i++) {
    if (++noise_div == NOISE_DIV) {
      noise_div = 0;
      noise_clock();
    }

    const float shell = shell_f.step(shell_rc.step(latch & BZ_SHELL, n31), loud);
    const float explo = explo_f.step(explo_rc.step(latch & BZ_EXPLO, n34), loud);
    const float engine = engine_step(latch);

    if (!(latch & BZ_SOUND_EN)) {
      out[i] = 0;
      continue;
    }

    const float poke = pokey[i] * POKEY_VOLTS;
    float v = (shell / R_SHELL + explo / R_EXPLO + engine / R_ENGINE + poke / R_POKEY) / G_TOTAL;
    dc_cap += (v - dc_cap) * dc_a;
    v -= dc_cap;

    const int32_t s = (int32_t)(v * OUT_GAIN);
    out[i] = s > SAMPLE_MAX ? SAMPLE_MAX : (s < -SAMPLE_MAX ? -SAMPLE_MAX : s);
  }
}
