#include "marble_opm.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include <string.h>

// OPM registers (ymfm opm_registers)
static const uint8_t OPM_TEST = 0x01, OPM_KEY = 0x08, OPM_LFRQ = 0x18, OPM_DEPTH = 0x19;
static const uint8_t OPM_WAVE = 0x1b, OPM_CONNECT = 0x20, OPM_KC = 0x28, OPM_PMS = 0x38;
static const uint8_t OPM_OP = 0x40;
static const uint8_t LFO_RESET = 0x02, DEPTH_PM = 0x80;
// YM2203 registers
static const uint8_t OPN_KEY = 0x28, OPN_MUL = 0x30, OPN_TL = 0x40, OPN_AR = 0x50;
static const uint8_t OPN_D1R = 0x60, OPN_D2R = 0x70, OPN_RR = 0x80;
static const uint8_t OPN_FNUM = 0xa0, OPN_BLOCK = 0xa4, OPN_CONNECT = 0xb0;

static const uint8_t TL_MAX = 0x7f;
static const uint8_t TL_ONE_SIDE = 8;  // 6 dB: one of L / R in the mono mix

// 1/64 semitone steps: KF bits 7-2; an octave of the key code is 12 semitones from C#
static const int16_t STEPS = 64, OCTAVE = 12 * STEPS, TOP = 8 * OCTAVE - 1;
// note code (KC bits 3-0) -> semitone above C#; codes 3, 7, 11, 15 do not exist
static const uint8_t NOTE_SEMI[16] = {0, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11, 11};
// F-number of semitone s (C# .. the next C#) one block below the OPM octave:
// 440 Hz * 2^((s - 8) / 12) at 4.027 MHz / 72
static const uint16_t FNUM[13] = {1299, 1376, 1458, 1545, 1637, 1734, 1837, 1947, 2062, 2185,
                                  2315, 2452, 2598};
static const uint16_t FNUM_MAX = 0x7ff;

// carriers per algorithm, bit k = operator at register offset 8k (M1, M2, C1, C2)
static const uint8_t CARRIERS[8] = {0x8, 0x8, 0x8, 0x8, 0xc, 0xe, 0xe, 0xf};
// DT2 multipliers x 1000 (1, sqrt 2, 1.57, 1.73)
static const uint16_t DT2_RATIO[4] = {1000, 1414, 1570, 1730};

// LFO: counter += (16 + LFRQ low) << LFRQ high per 55.9 kHz tick, 30 bit cycle (ymfm
// clock_noise_and_lfo); ticks per 60 Hz frame
static const uint32_t LFO_TICKS = 932, LFO_MASK = (1u << 30) - 1;
static const uint8_t LFO_SHIFT = 22;
enum LfoWave : uint8_t { LFO_SAW, LFO_SQUARE, LFO_TRIANGLE, LFO_NOISE };
// PMS 0-7 at full depth: 0, 5, 10, 20, 50, 100, 400, 700 cents, in 1/640 semitone
static const int32_t PMS_RANGE[8] = {0, 32, 64, 128, 320, 640, 2560, 4480};
static const int32_t WAVE_MAX = 127, DEPTH_MAX = 127, PMS_SCALE = 10;

void MarbleOpm::reset() {
  memset(regs, 0, sizeof(regs));
  memset(pm, 0, sizeof(pm));
  pmd = 0;
  lfo_phase = 0;
}

void MarbleOpm::write(uint8_t reg, uint8_t value) {
  if (reg == OPM_DEPTH) {  // PMD and AMD share the address; AM is dropped
    if (value & DEPTH_PM) { pmd = value & ~DEPTH_PM; }
    return;
  }
  regs[reg] = value;
  if (reg == OPM_TEST && (value & LFO_RESET)) {
    lfo_phase = 0;
    return;
  }

  if (reg == OPM_KEY) {
    const uint8_t ch = value & 7;
    if (ch < FIRST) { return; }
    fm.writeData(OPN_KEY, (uint8_t)(((value >> 3) & 0xf) << 4 | (ch - FIRST)));
    return;
  }
  if (reg < OPM_CONNECT || (reg & 7) < FIRST) { return; }

  const uint8_t c = (reg & 7) - FIRST;
  if (reg < OPM_KC) {  // RL, feedback, algorithm: carriers may change
    fm.writeData(OPN_CONNECT + c, value & 0x3f);
    for (uint8_t k = 0; k < 4; k++) { opLevel(c, k); }
    return;
  }
  if (reg < OPM_PMS) {  // key code, key fraction
    pitch(c);
    return;
  }
  if (reg < OPM_OP) { return; }  // PMS / AMS: frame()
  opWrite(reg, value);
}

// operator registers: OPM base + channel + 8k -> YM2203 base + channel + 4k
void MarbleOpm::opWrite(uint8_t reg, uint8_t value) {
  const uint8_t c = (reg & 7) - FIRST, k = (reg >> 3) & 3, at = c + 4 * k;
  switch (reg & 0xe0) {
    case 0x40: opMul(c, k); break;                                  // DT1, MUL
    case 0x60: opLevel(c, k); break;                                // TL
    case 0x80: fm.writeData(OPN_AR + at, value & 0xdf); break;      // KS, AR
    case 0xa0: fm.writeData(OPN_D1R + at, value & 0x1f); break;     // AM enable, D1R
    case 0xc0:                                                      // DT2, D2R
      fm.writeData(OPN_D2R + at, value & 0x1f);
      opMul(c, k);
      break;
    default: fm.writeData(OPN_RR + at, value); break;               // D1L, RR
  }
}

// DT1 as is; MUL times the DT2 ratio, rounded (MUL 0 = 0.5)
void MarbleOpm::opMul(uint8_t c, uint8_t k) {
  const uint8_t r = OPM_OP + c + FIRST + 8 * k;
  const uint8_t dt1_mul = regs[r], dt2 = regs[r + 0x80] >> 6;
  uint32_t mul = dt1_mul & 0xf;
  if (dt2) {
    const uint32_t halves = mul ? mul * 2 : 1;
    mul = (halves * DT2_RATIO[dt2] + 1000) / 2000;
    if (mul < 1) { mul = 1; }
    if (mul > 15) { mul = 15; }
  }
  fm.writeData(OPN_MUL + c + 4 * k, (uint8_t)((dt1_mul & 0x70) | mul));
}

// TL; carriers quieter by the RL bits: both outputs 0 dB, one -6 dB, none silent
void MarbleOpm::opLevel(uint8_t c, uint8_t k) {
  const uint8_t connect = regs[OPM_CONNECT + c + FIRST];
  uint32_t tl = regs[0x60 + c + FIRST + 8 * k] & TL_MAX;
  if (CARRIERS[connect & 7] & (1 << k)) {
    const uint8_t rl = connect >> 6;
    if (rl == 0) { tl = TL_MAX; }
    if (rl == 1 || rl == 2) { tl += TL_ONE_SIDE; }
  }
  fm.writeData(OPN_TL + c + 4 * k, (uint8_t)(tl > TL_MAX ? TL_MAX : tl));
}

// key code + fraction + LFO offset -> F-number / block, interpolated between semitones
void MarbleOpm::pitch(uint8_t c) {
  const uint8_t kc = regs[OPM_KC + c + FIRST], kf = regs[OPM_KC + 8 + c + FIRST];
  int32_t pos = (((kc >> 4) & 7) * 12 + NOTE_SEMI[kc & 0xf]) * STEPS + (kf >> 2) + pm[c];
  if (pos < 0) { pos = 0; }
  if (pos > TOP) { pos = TOP; }

  const int32_t octave = pos / OCTAVE, s = (pos % OCTAVE) / STEPS, frac = pos % STEPS;
  uint32_t fnum = FNUM[s] + (((FNUM[s + 1] - FNUM[s]) * frac) >> 6);
  int32_t block = octave - 1;
  if (block < 0 || fnum > FNUM_MAX) {
    fnum = (fnum + 1) >> 1;
    block++;
  }
  fm.writeData(OPN_BLOCK + c, (uint8_t)(block << 3 | fnum >> 8));  // latched by the F-number
  fm.writeData(OPN_FNUM + c, (uint8_t)fnum);
}

// -127 .. 127 at the current LFO phase
int16_t MarbleOpm::lfoWave() const {
  const int16_t p = (lfo_phase >> LFO_SHIFT) & 0xff;
  switch (regs[OPM_WAVE] & 3) {
    case LFO_SAW: return p < 0x80 ? p : p - 0x100 + 1;
    case LFO_SQUARE: return p < 0x80 ? WAVE_MAX : -WAVE_MAX;
    case LFO_TRIANGLE: return p < 0x80 ? 2 * p - WAVE_MAX : WAVE_MAX - 2 * (p - 0x80);
    default: return 0;  // noise: unused by Marble's effects
  }
}

void MarbleOpm::frame() {
  if (!(regs[OPM_TEST] & LFO_RESET)) {
    const uint8_t lfrq = regs[OPM_LFRQ];
    lfo_phase = (lfo_phase + ((16u + (lfrq & 15)) << (lfrq >> 4)) * LFO_TICKS) & LFO_MASK;
  }
  const int32_t w = lfoWave();

  for (uint8_t c = 0; c < CHANNELS; c++) {
    const uint8_t pms = (regs[OPM_PMS + c + FIRST] >> 4) & 7;
    const int16_t off = (int16_t)(w * pmd * PMS_RANGE[pms] / (WAVE_MAX * DEPTH_MAX * PMS_SCALE));
    if (off == pm[c]) { continue; }
    pm[c] = off;
    pitch(c);
  }
}

#endif  // GAME_MARBLE
