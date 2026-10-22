#include "votrax.h"

#include <math.h>
#include <string.h>

// Port of MAME src/devices/sound/votrax.cpp. Names follow MAME.

const uint32_t VotraxSc01::TABLE_BYTES;

// Glottal wave (resistor ladder), rescaled to middle 0, top 1
static const votrax_real GLOTTAL[9] = {
    0, -4 / 7.0f, 7 / 7.0f, 6 / 7.0f, 5 / 7.0f, 4 / 7.0f, 3 / 7.0f, 2 / 7.0f, 1 / 7.0f};

// glottal closure amplitude, (7 ^ (closure >> 2)) / 7
static const votrax_real CLOSURE_GAIN[8] = {
    7 / 7.0f, 6 / 7.0f, 5 / 7.0f, 4 / 7.0f, 3 / 7.0f, 2 / 7.0f, 1 / 7.0f, 0};

static double bitsToCaps(uint32_t value, const double *caps, int n) {
  double total = 0;
  for (int i = 0; i < n; i++) {
    if (value & 1) {
      total += caps[i];
    }
    value >>= 1;
  }
  return total;
}

// Analog filter builders (MAME build_*_filter), double, raw a/b.
// MAME's cclock = clock / 36, sclock = clock / 18.

static void buildStandard(double sclock, double cclock, double *a, double *b, double c1t, double c1b,
                          double c2t, double c2b, double c3, double c4) {
  const double k0 = c1t / (cclock * c1b);
  const double k1 = c4 * c2t / (cclock * c1b * c3);
  const double k2 = c4 * c2b / (cclock * cclock * c1b * c3);

  const double fpeak = sqrt(fabs(k0 * k1 - k2)) / (2 * M_PI * k2);
  const double zc = 2 * M_PI * fpeak / tan(M_PI * fpeak / sclock);

  const double m0 = zc * k0;
  const double m1 = zc * k1;
  const double m2 = zc * zc * k2;

  a[0] = 1 + m0;
  a[1] = 3 + m0;
  a[2] = 3 - m0;
  a[3] = 1 - m0;
  b[0] = 1 + m1 + m2;
  b[1] = 3 + m1 - m2;
  b[2] = 3 - m1 - m2;
  b[3] = 1 - m1 + m2;
}

static void buildLowpass(double sclock, double cclock, double *a, double *b, double c1t, double c1b) {
  const double k = c1b / (cclock * c1t) * (150.0 / 4000.0);
  const double fpeak = 1 / (2 * M_PI * k);
  const double zc = 2 * M_PI * fpeak / tan(M_PI * fpeak / sclock);
  const double m = zc * k;

  a[0] = 1;
  b[0] = 1 + m;
  b[1] = 1 - m;
}

static void buildNoiseShaper(double sclock, double cclock, double *a, double *b, double c1, double c2t,
                             double c2b, double c3, double c4) {
  const double k0 = c2t * c3 * c2b / c4;
  const double k1 = c2t * (cclock * c2b);
  const double k2 = c1 * c2t * c3 / (cclock * c4);

  const double fpeak = sqrt(1 / k2) / (2 * M_PI);
  const double zc = 2 * M_PI * fpeak / tan(M_PI * fpeak / sclock);

  const double m0 = zc * k0;
  const double m1 = zc * k1;
  const double m2 = zc * zc * k2;

  a[0] = m0;
  a[1] = 0;
  a[2] = -m0;
  b[0] = 1 + m1 + m2;
  b[1] = 2 - 2 * m2;
  b[2] = 1 - m1 + m2;
}

static void normalize(VotraxCoef &out, const double *a, const double *b) {
  for (int i = 0; i < 4; i++) {
    out.a[i] = (votrax_real)(a[i] / b[0]);
  }
  for (int i = 0; i < 3; i++) {
    out.b[i] = (votrax_real)(b[i + 1] / b[0]);
  }
}

// F1 / F2 voice / F3 parameter indexed caps, as in MAME filters_commit
static void buildF1(double s, double c, VotraxCoef &out, uint32_t f1) {
  static const double caps[] = {2546, 4973, 9861, 19724};
  double a[4], b[4];
  buildStandard(s, c, a, b, 11247, 11797, 949, 52067, 2280 + bitsToCaps(f1, caps, 4), 166272);
  normalize(out, a, b);
}

static void buildF2(double s, double c, VotraxCoef &out, uint32_t f2, uint32_t f2q) {
  static const double qcaps[] = {1390, 2965, 5875, 11297};
  static const double fcaps[] = {833, 1663, 3164, 6327, 12654};
  double a[4], b[4];
  buildStandard(s, c, a, b, 24840, 29154, 829 + bitsToCaps(f2q, qcaps, 4), 38180,
                2352 + bitsToCaps(f2, fcaps, 5), 34270);
  normalize(out, a, b);
}

static void buildF3(double s, double c, VotraxCoef &out, uint32_t f3) {
  static const double caps[] = {2226, 4485, 9056, 18111};
  double a[4], b[4];
  buildStandard(s, c, a, b, 0, 17594, 868, 18828, 8480 + bitsToCaps(f3, caps, 4), 50019);
  normalize(out, a, b);
}

void VotraxSc01::buildTables() {
  // coefficients do not depend on the clock (cutoffs scale with it), so the
  // 756 kHz build serves both selects
  const double sclock = CLOCK_LO / 18.0, cclock = CLOCK_LO / 36.0;

  for (uint32_t i = 0; i < F1_ENTRIES; i++) {
    buildF1(sclock, cclock, f1[i], i);
  }
  for (uint32_t q = 0; q < 16; q++) {
    for (uint32_t f = 0; f < 32; f++) {
      buildF2(sclock, cclock, f2v[(f << 4) | q], f, q);
    }
  }
  for (uint32_t i = 0; i < F3_ENTRIES; i++) {
    buildF3(sclock, cclock, f3[i], i);
  }

  double a[4], b[4];
  buildStandard(sclock, cclock, a, b, 0, 28810, 1165, 21457, 8558, 7289);
  normalize(f4, a, b);

  buildLowpass(sclock, cclock, a, b, 1122, 23131);
  fxA = (votrax_real)(a[0] / b[0]);
  fxB = (votrax_real)(b[1] / b[0]);

  // the noise shaper's poles move with the clock
  static const uint32_t clocks[2] = {CLOCK_LO, CLOCK_HI};
  for (int i = 0; i < 2; i++) {
    buildNoiseShaper(clocks[i] / 18.0, clocks[i] / 36.0, a, b, 15500, 14854, 8450, 9523, 14083);
    fnSet[i].a0 = (votrax_real)(a[0] / b[0]);
    fnSet[i].b1 = (votrax_real)(b[1] / b[0]);
    fnSet[i].b2 = (votrax_real)(b[2] / b[0]);
  }
}

void VotraxSc01::init(const uint8_t *rom512, uint8_t *arena) {
  rom = rom512;
  VotraxCoef *t = (VotraxCoef *)arena;
  if (tablesBuilt && f1 == t) {
    return;
  }
  f1 = t;
  f2v = f1 + F1_ENTRIES;
  f3 = f2v + F2_ENTRIES;
  buildTables();
  tablesBuilt = true;
}

void VotraxSc01::reset() {
  // no reset on the real chip; start with something sane (MAME device_reset)
  phone = 0x3f;
  inflection = 0;
  ar = true;
  sampleCount = 0;
  clockHz = CLOCK_LO;
  fn = &fnSet[0];
  clkAcc = sampleAcc = 0;
  timerParam = T_NONE;
  timerLeft = 0;
  quiet = 0;

  phoneCommit();

  curFa = curFc = curVa = 0;
  curF1 = curF2 = curF2q = curF3 = 0;
  filtersCommit(true);

  pitch = 0;
  closure = 0;
  updateCounter = 0;
  curClosure = true;
  noise = 0;
  curNoise = false;

  memset(hGlot, 0, sizeof(hGlot));
  memset(hF1, 0, sizeof(hF1));
  memset(hF2, 0, sizeof(hF2));
  memset(hF3, 0, sizeof(hF3));
  memset(hF4in, 0, sizeof(hF4in));
  memset(hF4, 0, sizeof(hF4));
  fxY = nX1 = nX2 = nY1 = nY2 = 0;

  idle = (romVa | romFa) == 0;
}

void VotraxSc01::write(uint8_t data) {
  setClock((data & 0x40) ? CLOCK_HI : CLOCK_LO);
  writePhone(data & 0x3f, (data & 0x80) ? 0 : 2);
}

void VotraxSc01::setClock(uint32_t hz) {
  clockHz = hz;
  if (hz == CLOCK_LO || hz == CLOCK_HI) {
    fn = &fnSet[hz == CLOCK_HI];
    return;
  }
  if (hz != fnVarHz) {
    double a[4], b[4];
    buildNoiseShaper(hz / 18.0, hz / 36.0, a, b, 15500, 14854, 8450, 9523, 14083);
    fnVar.a0 = (votrax_real)(a[0] / b[0]);
    fnVar.b1 = (votrax_real)(b[1] / b[0]);
    fnVar.b2 = (votrax_real)(b[2] / b[0]);
    fnVarHz = hz;
  }
  fn = &fnVar;
}

void VotraxSc01::writePhone(uint8_t ph, uint8_t infl) {
  inflection = infl & 3;
  phone = ph & 0x3f;
  ar = false;

  // commit about 0.1 ms from now, unless one is already pending
  if (timerParam != T_COMMIT_PHONE) {
    timerParam = T_COMMIT_PHONE;
    timerLeft = COMMIT_CLOCKS;
  }
  idle = false;
  quiet = 0;
}

void VotraxSc01::fireTimer() {
  if (timerParam == T_COMMIT_PHONE) {
    phoneCommit();
    timerParam = T_END_OF_PHONE;
    timerLeft = 16 * ((uint32_t)romDuration * 4 + 1) * 4 * 9 + 2;
  } else {
    ar = true;
    timerParam = T_NONE;
  }
}

static inline uint32_t bit(uint64_t v, int n) { return (uint32_t)((v >> n) & 1); }

void VotraxSc01::phoneCommit() {
  // only these two counters are reset on a phone change
  phoneTick = 0;
  ticks = 0;

  for (int i = 0; i < 64; i++) {
    uint64_t val = 0;
    for (int k = 7; k >= 0; k--) {
      val = (val << 8) | rom[i * 8 + k];
    }
    if (phone != ((val >> 56) & 0x3f)) {
      continue;
    }

    // bitswap<4>(val, a, b, c, d): a is the MSB
#define BITS4(a, b, c, d) \
  (uint8_t)((bit(val, a) << 3) | (bit(val, b) << 2) | (bit(val, c) << 1) | bit(val, d))
    romF1 = BITS4(0, 7, 14, 21);
    romVa = BITS4(1, 8, 15, 22);
    romF2 = BITS4(2, 9, 16, 23);
    romFc = BITS4(3, 10, 17, 24);
    romF2q = BITS4(4, 11, 18, 25);
    romF3 = BITS4(5, 12, 19, 26);
    romFa = BITS4(6, 13, 20, 27);

    // these two have inverted bit orders (prototype miswiring)
    romCld = BITS4(34, 32, 30, 28);
    romVd = BITS4(35, 33, 31, 29);
#undef BITS4

    romClosure = bit(val, 36) != 0;
    // bitswap<7>(~val, 37..43), 37 is the MSB
    uint8_t dur = 0;
    for (int k = 0; k < 7; k++) {
      dur = (uint8_t)((dur << 1) | (bit(val, 37 + k) ^ 1));
    }
    romDuration = dur;

    // hard-wired on the die
    romPause = (phone == 0x03) || (phone == 0x3e);

    if (romCld == 0) {
      curClosure = romClosure;
    }
    return;
  }
}

// one interpolation step: adds an eighth of the distance to the target
static inline void interpolate(uint8_t &reg, uint8_t target) {
  reg = (uint8_t)(reg - (reg >> 3) + (target << 1));
}

// called at cclock (main / 36), every other sample
void VotraxSc01::chipUpdate() {
  // phone tick counter, stopped at 16 ticks
  if (ticks != 0x10) {
    phoneTick++;
    if (phoneTick == (uint16_t)((romDuration << 2) | 1)) {
      phoneTick = 0;
      ticks++;
      if (ticks == romCld) {
        curClosure = romClosure;
      }
    }
  }

  // 625 Hz (/16) and 208 Hz (/48) update timing, 208 between two 625
  updateCounter++;
  if (updateCounter == 0x30) {
    updateCounter = 0;
  }
  const bool tick625 = !(updateCounter & 0xf);
  const bool tick208 = updateCounter == 0x28;

  // formants; die bug: fc is updated here, not va. Frozen on a pause
  // phone unless both volumes are zero.
  if (tick208 && (!romPause || !(filtFa || filtVa))) {
    interpolate(curFc, romFc);
    interpolate(curF1, romF1);
    interpolate(curF2, romF2);
    interpolate(curF2q, romF2q);
    interpolate(curF3, romF3);
  }

  // non-formant; same bug the other way
  if (tick625) {
    if (ticks >= romVd) {
      interpolate(curFa, romFa);
    }
    if (ticks >= romCld) {
      interpolate(curVa, romVa);
    }
  }

  // glottal closure counter, used immediately in the analog path
  if (!curClosure && (filtFa || filtVa)) {
    closure = 0;
  } else if (closure != 7 << 2) {
    closure++;
  }

  // pitch counter (equality compare, wraps if missed), +2 for the delay
  pitch = (uint8_t)(pitch + 1);
  if ((int)pitch == ((0xe0 ^ (inflection << 5) ^ (filtF1 << 1)) + 2)) {
    pitch = 0;
  }

  // filters are updated in index 1 of the pitch wave, four times in a row
  if ((pitch & 0xf9) == 0x08) {
    filtersCommit(false);
  }

  // 15 bit noise shift register, nxor on the last two bits
  const bool inp = curNoise && (noise != 0x7fff);
  noise = (uint16_t)(((noise << 1) & 0x7ffe) | inp);
  curNoise = !(((noise >> 14) ^ (noise >> 13)) & 1);
}

void VotraxSc01::filtersCommit(bool force) {
  filtFa = curFa >> 4;
  filtFc = curFc >> 4;
  filtVa = curVa >> 4;

  if (force || filtF1 != (curF1 >> 4)) {
    filtF1 = curF1 >> 4;
    f1c = &f1[filtF1];
  }
  if (force || filtF2 != (curF2 >> 3) || filtF2q != (curF2q >> 4)) {
    filtF2 = curF2 >> 3;
    filtF2q = curF2q >> 4;
    f2c = &f2v[(filtF2 << 4) | filtF2q];
  }
  if (force || filtF3 != (curF3 >> 4)) {
    filtF3 = curF3 >> 4;
    f3c = &f3[filtF3];
  }

  vaGain = filtVa / 15.0f;
  faGain = filtFa / 15.0f;
  noiseInject = (5 + (15 ^ filtFc)) / 20.0f;
}

static inline void push4(votrax_real *h, votrax_real v) {
  h[3] = h[2];
  h[2] = h[1];
  h[1] = h[0];
  h[0] = v;
}

static inline void push3(votrax_real *h, votrax_real v) {
  h[2] = h[1];
  h[1] = h[0];
  h[0] = v;
}

// y = sum(x[i] * a[i]) - sum(y[i] * b[i]); x[0] is the current input
static inline votrax_real iir3(const votrax_real *x, const votrax_real *y, const VotraxCoef &c) {
  return c.a[0] * x[0] + c.a[1] * x[1] + c.a[2] * x[2] + c.a[3] * x[3] - c.b[0] * y[0] -
         c.b[1] * y[1] - c.b[2] * y[2];
}

votrax_real VotraxSc01::analogCalc() {
  // voice: glottal wave -> linear amp -> F1 -> F2 (voice half)
  votrax_real v = pitch >= (9 << 3) ? 0 : GLOTTAL[pitch >> 3];
  v *= vaGain;
  push4(hGlot, v);

  v = iir3(hGlot, hF1, *f1c);
  push4(hF1, v);

  v = iir3(hF1, hF2, *f2c);
  push4(hF2, v);

  // noise: shaper input, linear amplitude. MAME's F2 noise injection stage
  // is neutralized (outputs 0), so only the shaper output is used.
  votrax_real n = ((pitch & 0x40) && curNoise) ? 1e4f : -1e4f;
  n *= faGain;
  const votrax_real ns = fn->a0 * (n - nX2) - fn->b1 * nY1 - fn->b2 * nY2;
  nX2 = nX1;
  nX1 = n;
  nY2 = nY1;
  nY1 = ns;

  // mixed: F3 of the F2 voice output, second noise insertion, F4
  votrax_real vn = iir3(hF2, hF3, *f3c);
  push3(hF3, vn);

  vn += ns * noiseInject;
  push4(hF4in, vn);

  vn = iir3(hF4in, hF4, f4);
  push3(hF4, vn);

  // glottal closure amplitude, then the final fixed lowpass
  vn *= CLOSURE_GAIN[closure >> 2];
  vn = fxA * vn - fxB * fxY;
  fxY = vn;

  return vn * 0.35f;
}

void VotraxSc01::goIdle() {
  // settle the interpolation as the silent chip would have, then drop the
  // (inaudible) filter ringing
  for (int i = 0; i < 128; i++) {
    interpolate(curFa, romFa);
    interpolate(curVa, romVa);
    interpolate(curFc, romFc);
    interpolate(curF1, romF1);
    interpolate(curF2, romF2);
    interpolate(curF2q, romF2q);
    interpolate(curF3, romF3);
  }
  filtersCommit(false);
  memset(hGlot, 0, sizeof(hGlot));
  memset(hF1, 0, sizeof(hF1));
  memset(hF2, 0, sizeof(hF2));
  memset(hF3, 0, sizeof(hF3));
  memset(hF4in, 0, sizeof(hF4in));
  memset(hF4, 0, sizeof(hF4));
  fxY = nX1 = nX2 = nY1 = nY2 = 0;
  idle = true;
  sampleAcc = 0;
}

int16_t VotraxSc01::sample() {
  sampleCount++;
  if (sampleCount & 1) {
    chipUpdate();
  }
  // 1.0 -> 16384: Gorf phrases peak near 1.0, headroom keeps the clamp idle
  const votrax_real o = analogCalc() * (votrax_real)FULL_SCALE;
  int32_t s = o > 32767.0f ? 32767 : (o < -32767.0f ? -32767 : (int32_t)o);

  if (s == 0 && timerParam == T_NONE && ((filtVa | filtFa | romVa | romFa) == 0)) {
    if (++quiet >= QUIET_SAMPLES) {
      goIdle();
    }
  } else {
    quiet = 0;
  }
  return (int16_t)s;
}
