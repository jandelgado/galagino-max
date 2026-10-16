#include "pleiads_sound.h"

// MAME pleiads.cpp constants (pleiads_sound_device::device_start)
static const int VMIN = 0, VMAX = 32767;
static const int RATE = 24000;
static const int TONE1_CLOCK = 8000;
static const int PC4_MIN = VMAX * 7 / 50;
static const int PA5_R = 33, PC5_R = 47;          // kOhm
static const int POLYBIT_R = 47, OPAMP_R = 20;    // kOhm
static const int NOISE_FREQ = 1412;
static const float PA6_POLY_DISCHARGE = 0.1f;

// mix: MAME routes the board at 0.40 and the TMS3615 at 0.75 (sum /
// 32768 each), scaled so the sum of both full scale stays near AUDIO_MAX
static const int GAIN_BOARD = 178, GAIN_TMS = 333;  // >> 15

PleiadsSound::PleiadsSound() {
  pa5.charge = 3.3f;      pa5.discharge = 2.2f;
  pa6.charge = 0.000726f; pa6.discharge = 0.022f;
  pb4.charge = 0.1f;      pb4.discharge = 0.1f;
  pc4.charge = 0.066f;    pc4.discharge = 0.022f;
  pc5.charge = 0.0033f;   pc5.discharge = 0.1f;
  pc4.level = PC4_MIN;
  t2.max_freq = 351;
  t3.max_freq = 582;
  t4.max_freq = 1315;
}

// MAME update_c_*: charge toward VMAX or discharge toward vmin, one step
// of the counter per sample (time constant in seconds). MAME's PC5 and PA5
// discharge add one sample period per step instead of n (fast discharge).
int PleiadsSound::charge(Cap &c, bool up, int vmin, Discharge mode) {
  if (up) {
    if (c.level < VMAX) {
      c.counter -= (int)((VMAX - c.level) / c.charge);
      if (c.counter <= 0) {
        const int n = (-c.counter / RATE) + 1;
        c.counter += n * RATE;
        c.level += n;
        if (c.level > VMAX) { c.level = VMAX; }
      }
    }
    return c.level;
  }
  if (c.level > vmin) {
    c.counter -= (int)((c.level - vmin) / c.discharge);
    if (c.counter <= 0) {
      const int n = (-c.counter / RATE) + 1;
      c.counter += (mode == DISCHARGE_FAST) ? RATE : n * RATE;
      c.level -= n;
      if (c.level < vmin) { c.level = vmin; }
    }
  }
  return c.level;
}

// fixed 8 kHz divided by 1 .. 15 (latch A bits 0-3, 15 = off)
int PleiadsSound::tone1() {
  if ((latch_a & 15) != 15) {
    t1.counter -= TONE1_CLOCK;
    while (t1.counter <= 0) {
      t1.counter += RATE;
      if (++t1.max_freq == 16) {
        t1.max_freq = latch_a & 15;
        t1.output ^= 1;
      }
    }
  }
  return t1.output ? VMAX : -VMAX;
}

// upper 556: two tones swept by the PB4 capacitor, latch B bit 5 gates
int PleiadsSound::tone23() {
  const int level = VMAX - charge(pb4, latch_b & 0x10, VMIN);
  if ((latch_b & 0x20) == 0) { return 0; }

  if (level < VMAX) {
    t2.counter -= t2.max_freq * level / 32768;
    if (t2.counter <= 0) {
      const int n = (-t2.counter / RATE) + 1;
      t2.counter += n * RATE;
      t2.output = (t2.output + n) & 1;
    }
    // MAME uses tone 2's counter for n here
    t3.counter -= t3.max_freq / 3 + t3.max_freq * 2 / 3 * level / 33768;
    if (t3.counter <= 0) {
      const int n = (-t2.counter / RATE) + 1;
      t3.counter += RATE;
      t3.output = (t3.output + n) & 1;
    }
  }
  return ((t2.output ? VMAX : -VMAX) + (t3.output ? VMAX : -VMAX)) / 2;
}

// lower 556 upper half: PC4 sweep divided by the polynomial bit, gating
// the PC5 / PA5 sweeps
int PleiadsSound::tone4() {
  int level = charge(pc4, latch_c & 0x10, PC4_MIN);
  const int vpc5 = charge(pc5, latch_c & 0x20, VMIN, DISCHARGE_FAST);
  const int vpa5 = charge(pa5, latch_a & 0x20, VMIN, DISCHARGE_FAST);

  if (polybit) {
    level = level + (VMAX - level) * OPAMP_R / (OPAMP_R + POLYBIT_R);
  } else {
    level = level * POLYBIT_R / (OPAMP_R + POLYBIT_R);
  }

  t4.counter -= t4.max_freq * level / 32768;
  if (t4.counter <= 0) {
    const int n = (-t4.counter / RATE) + 1;
    t4.counter += n * RATE;
    t4.output = (t4.output + n) & 1;
  }

  const int sum = vpc5 * PA5_R / (PA5_R + PC5_R) + vpa5 * PC5_R / (PA5_R + PC5_R);
  return t4.output ? sum : -sum;
}

// 4006 shift register noise; latch A bit 4 picks the clock, bits 6/7
// are gated by the polynomial bit
int PleiadsSound::noise() {
  int pa6_level;
  if (latch_a & 0x40) {
    pa6_level = charge(pa6, true, VMIN);
  } else if (polybit) {
    const float d = pa6.discharge;
    pa6.discharge = PA6_POLY_DISCHARGE;   // only the poly bit discharges: 10uF / 10k
    pa6_level = charge(pa6, false, VMIN);
    pa6.discharge = d;
  } else {
    pa6_level = pa6.level;
  }

  noise_counter -= (latch_a & 0x10) ? NOISE_FREQ * 2 / 3 : NOISE_FREQ / 3;
  if (noise_counter <= 0) {
    const int n = (-noise_counter / RATE) + 1;
    noise_counter += n * RATE;
    // MAME poly18 table: bit k = bit 0 of the shift register after k steps
    for (int i = 0; i < n; i++) {
      const bool same = ((lfsr >> 16) & 1) == ((lfsr >> 17) & 1);
      lfsr = ((lfsr << 1) | (same ? 1 : 0)) & 0x3FFFF;
    }
    polybit = lfsr & 1;
  }

  int sum = polybit ? pa6_level : -pa6_level;
  if (latch_a & 0x80) { sum += polybit ? VMAX : -VMAX; }
  return sum / 2;
}

void PleiadsSound::render(const uint8_t *regs, int16_t *out, int n) {
  // latch B change = TMS3615 note write (MAME control_b_w)
  if (regs[1] != latch_b || first) {
    if (!first) {
      int pitch = (regs[1] >> 6) & 3;
      if (pitch == 3) { pitch = 2; }
      tms.note(pitch, regs[1] & 15);
    }
    first = false;
  }
  latch_a = regs[0];
  latch_b = regs[1];
  latch_c = regs[2];

  for (int i = 0; i < n; i++) {
    int board = tone1() / 2 + tone23() / 2 + tone4() + noise();
    if (board > 32767) { board = 32767; }
    if (board < -32768) { board = -32768; }
    const int32_t v = (board * GAIN_BOARD + dc.filter(tms.sample()) * GAIN_TMS) >> 15;
    out[i] = (int16_t)(v > 511 ? 511 : (v < -511 ? -511 : v));
  }
}

// ---- TMS3615 (tms36xx.cpp, tune4 = 13 notes x 6 footages) ----

static const int TMS_VMAX = 0x7FFF;
static const int TMS_CLOCK = 247;
static const int FSCALE = 1024;
// voices with a decay (MAME set_decays 0.33, 0.33, 0, 0.33, 0, 0.33)
static const bool VOICE_ON[6] = {true, true, false, true, false, true};
static const int DECAY = (int)(TMS_VMAX / 0.33);
static const int ACTIVE = 8;   // MAME m_voices: 4 enabled voices x 2 banks

#define TN(f, n) ((int)((FSCALE << ((n) - 1)) * (f)))
#define TA(f, n) ((int)((FSCALE << (n)) * (f)))
static const int TUNE4[13][6] = {
  {TA(1.12246, 0), TA(1.12246, 1), TN(1.41421, 2), TA(1.12246, 2), TN(1.41421, 3), TA(1.12246, 3)},
  {TN(1.18921, 1), TN(1.18921, 2), TN(1.49831, 2), TN(1.18921, 3), TN(1.49831, 3), TN(1.18921, 4)},
  {TN(1.25992, 1), TN(1.25992, 2), TN(1.58740, 2), TN(1.25992, 3), TN(1.58740, 3), TN(1.25992, 4)},
  {TN(1.33484, 1), TN(1.33484, 2), TN(1.68179, 2), TN(1.33484, 3), TN(1.68179, 3), TN(1.33484, 4)},
  {TN(1.41421, 1), TN(1.41421, 2), TN(1.78180, 2), TN(1.41421, 3), TN(1.78180, 3), TN(1.41421, 4)},
  {TN(1.49831, 1), TN(1.49831, 2), TN(1.88775, 2), TN(1.49831, 3), TN(1.88775, 3), TN(1.49831, 4)},
  {TN(1.58740, 1), TN(1.58740, 2), TA(1.0, 2),     TN(1.58740, 3), TA(1.0, 3),     TN(1.58740, 4)},
  {TN(1.68179, 1), TN(1.68179, 2), TA(1.05946, 2), TN(1.68179, 3), TA(1.05946, 3), TN(1.68179, 4)},
  {TN(1.78180, 1), TN(1.78180, 2), TA(1.12246, 2), TN(1.78180, 3), TA(1.12246, 3), TN(1.78180, 4)},
  {TN(1.88775, 1), TN(1.88775, 2), TN(1.18921, 3), TN(1.88775, 3), TN(1.18921, 4), TN(1.88775, 4)},
  {TA(1.0, 1),     TA(1.0, 2),     TN(1.25992, 3), TA(1.0, 3),     TN(1.25992, 4), TA(1.0, 4)},
  {TA(1.05946, 1), TA(1.05946, 2), TN(1.33484, 3), TA(1.05946, 3), TN(1.33484, 4), TA(1.05946, 4)},
  {TA(1.12246, 1), TA(1.12246, 2), TN(1.41421, 3), TA(1.12246, 3), TN(1.41421, 4), TA(1.12246, 4)},
};
#undef TN
#undef TA

// MAME tms36xx_note_w: notes 13-15 are ignored, counters restart
void PleiadsSound::Tms3615::note(int oct, int n) {
  if (n > 12) { return; }
  for (int v = 0; v < VOICES; v++) {
    vol_counter[v] = 0;
    counter[v] = 0;
  }
  octave = oct & 3;
  note_ofs = n;
  restart = true;
  playing = true;
}

int PleiadsSound::Tms3615::sample() {
  if (!playing) { return 0; }

  int sum = 0;
  for (int v = 0; v < VOICES; v++) {
    if (!VOICE_ON[v % 6] || vol[v] <= 0) { continue; }
    vol_counter[v] -= DECAY;
    while (vol_counter[v] <= 0) {
      vol_counter[v] += RATE;
      if (vol[v]-- <= 0) {
        frequency[v] = 0;
        vol[v] = 0;
        break;
      }
    }
  }

  // speed 0: the note starts on the first sample after the write, in the
  // other bank of six voices, so the previous note keeps decaying
  if (restart) {
    restart = false;
    shift ^= 6;
    for (int v = 0; v < 6; v++) {
      frequency[shift + v] = TUNE4[note_ofs][v] * (TMS_CLOCK << octave) / FSCALE;
      vol[shift + v] = TMS_VMAX;
    }
  }

  for (int v = 0; v < VOICES; v++) {
    if (!VOICE_ON[v % 6] || !frequency[v]) { continue; }
    counter[v] -= frequency[v];
    while (counter[v] <= 0) {
      counter[v] += RATE;
      output ^= 1 << v;
    }
    if (output & (1 << v)) { sum += vol[v]; }
  }
  return sum / ACTIVE;
}
