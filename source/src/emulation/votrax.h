#ifndef VOTRAX_H
#define VOTRAX_H

#include <stdint.h>

#if defined(ESP32) || defined(ESP_PLATFORM)
#  include "esp_attr.h"
#  define VOTRAX_IRAM IRAM_ATTR
#else
#  define VOTRAX_IRAM
#endif

#ifndef VOTRAX_REAL
#  define VOTRAX_REAL float
#endif
typedef VOTRAX_REAL votrax_real;

// Votrax SC-01 phoneme synthesizer, port of MAME votrax.cpp (Olivier
// Galibert's analog model). The 64 phones come from the chip's internal
// 512 byte ROM (sc01.bin); a digital core interpolates 8 parameters and
// drives a chain of switched-capacitor filters (glottal wave -> F1 -> F2
// -> F3 -> mixed with shaped noise -> F4 -> closure -> lowpass), run here
// in float at sclock = clock / 18 (42 kHz at 756 kHz).
//
// Differences from MAME:
//  - float instead of double; filter coefficients are built once in double
//    at init(), normalized by b[0], and looked up by parameter index
//    (the build functions are independent of the clock except the noise
//    shaper, which has one set per clock).
//  - MAME's F2 noise injection filter is "neutralized" (always outputs 0),
//    so its stage is not run.
//  - time is stepped per scanline (line()), event times are exact in main
//    clocks, but a write is seen at the next scanline boundary.
//  - analog work stops once the output has been silent (busy() false).
//
//   emulation core only: write(), request(), line(), busy()
//   audio core:          ring.render() (SpeechRingT, in_hz = sampleHz())
struct VotraxCoef {
  votrax_real a[4];  // numerator / b0
  votrax_real b[3];  // denominator b1..b3 / b0
};

class VotraxSc01 {
public:
  static const uint32_t CLOCK_LO = 756000;  // main clock, write bit 6 = 0
  static const uint32_t CLOCK_HI = 782000;  // write bit 6 = 1
  static const uint32_t LINE_HZ = 15734;    // Astrocade scanline rate

  static const uint32_t F1_ENTRIES = 16;
  static const uint32_t F2_ENTRIES = 32 * 16;  // (f2 << 4) | f2q
  static const uint32_t F3_ENTRIES = 16;
  static const uint32_t TABLE_BYTES = (F1_ENTRIES + F2_ENTRIES + F3_ENTRIES) * sizeof(VotraxCoef);

  // rom512: sc01.bin, must stay valid. arena: TABLE_BYTES, 4 byte aligned,
  // kept by the chip. Builds the coefficient tables (about tens of ms on an
  // ESP32); a second call with the same arena is a no-op.
  void init(const uint8_t *rom512, uint8_t *arena);
  void reset();

  // gorf port 0x17: bits 0-5 phone, bit 6 clock select (0: 756 kHz,
  // 1: 782 kHz), bit 7 inflection (0: 2, 1: 0)
  void write(uint8_t data);

  // phone (0-63) and inflection (0-3) as the chip pins see them
  void writePhone(uint8_t phone, uint8_t inflection);

  // main clock in Hz (Gottlieb: driven by a DAC, changes continuously);
  // the noise shaper is rebuilt for clocks other than CLOCK_LO / CLOCK_HI
  void setClock(uint32_t hz);

  // request line (MAME ar): low from write() until the phone is done
  bool request() const { return ar; }

  // false after the output has been silent for a while: line() then only
  // advances the request timer
  bool busy() const { return !idle; }

  // ring input rate (sclock) for the current clock select
  uint32_t sampleHz() const { return clockHz / 18; }

  // int16 value of analog output 1.0; 2x headroom before the clamp
  static const int32_t FULL_SCALE = 16384;

  // advance by one scanline; pushes int16 samples (FULL_SCALE = 1.0) at
  // sampleHz() into ring, 2.67 per line
  template <class Ring> void line(Ring &ring) { advance(1, LINE_HZ, ring); }

  // advance by ticks of a tickHz clock (ticks * clock must fit 32 bits);
  // samples into sink.push(int16_t) as line()
  template <class Sink> void advance(uint32_t ticks, uint32_t tickHz, Sink &sink);

  // test access
  const VotraxCoef &f1Coef(uint32_t i) const { return f1[i]; }
  const VotraxCoef &f2Coef(uint32_t i) const { return f2v[i]; }
  const VotraxCoef &f3Coef(uint32_t i) const { return f3[i]; }

private:
  struct NoiseCoef {
    votrax_real a0;  // a = {a0, 0, -a0}
    votrax_real b1, b2;
  };

  enum : uint8_t { T_NONE, T_COMMIT_PHONE, T_END_OF_PHONE };
  static const uint32_t SAMPLE_CLOCKS = 18;
  static const uint32_t COMMIT_CLOCKS = 72;
  static const uint16_t QUIET_SAMPLES = 256;

  void buildTables();
  void phoneCommit();
  void filtersCommit(bool force);
  void chipUpdate();
  void goIdle();
  VOTRAX_IRAM int16_t sample();
  VOTRAX_IRAM votrax_real analogCalc();
  void fireTimer();

  const uint8_t *rom = nullptr;
  VotraxCoef *f1 = nullptr;   // [16]
  VotraxCoef *f2v = nullptr;  // [512]
  VotraxCoef *f3 = nullptr;   // [16]
  bool tablesBuilt = false;

  // fixed filters
  VotraxCoef f4;
  votrax_real fxA, fxB;  // y = fxA * x - fxB * y1
  NoiseCoef fnSet[2];    // per clock select
  NoiseCoef fnVar;       // any other clock (setClock)
  uint32_t fnVarHz = 0;
  const NoiseCoef *fn = nullptr;

  // time
  uint32_t clockHz = CLOCK_LO;
  uint32_t clkAcc = 0;       // main clocks owed, in tick units
  uint32_t sampleAcc = 0;    // main clocks since the last sample
  uint32_t timerLeft = 0;    // main clocks
  uint8_t timerParam = T_NONE;
  bool ar = true;
  bool idle = true;
  uint16_t quiet = 0;
  uint32_t sampleCount = 0;

  // inputs
  uint8_t inflection = 0;
  uint8_t phone = 0x3f;

  // unpacked ROM values of the committed phone
  uint8_t romDuration = 0;
  uint8_t romVd = 0, romCld = 0;
  uint8_t romFa = 0, romFc = 0, romVa = 0;
  uint8_t romF1 = 0, romF2 = 0, romF2q = 0, romF3 = 0;
  bool romClosure = false, romPause = false;

  // interpolated and committed parameters
  uint8_t curFa = 0, curFc = 0, curVa = 0;
  uint8_t curF1 = 0, curF2 = 0, curF2q = 0, curF3 = 0;
  uint8_t filtFa = 0, filtFc = 0, filtVa = 0;
  uint8_t filtF1 = 0, filtF2 = 0, filtF2q = 0, filtF3 = 0;

  // counters
  uint16_t phoneTick = 0;
  uint8_t ticks = 0;
  uint8_t pitch = 0;
  uint8_t closure = 0;
  uint8_t updateCounter = 0;

  bool curClosure = true;
  uint16_t noise = 0;
  bool curNoise = false;

  // analog state: committed filter pointers, derived gains, histories
  const VotraxCoef *f1c = nullptr;
  const VotraxCoef *f2c = nullptr;
  const VotraxCoef *f3c = nullptr;
  votrax_real vaGain = 0, faGain = 0, noiseInject = 0;

  votrax_real hGlot[4];  // amplified glottal wave
  votrax_real hF1[4];    // F1 out
  votrax_real hF2[4];    // F2 out (also F3 input)
  votrax_real hF3[3];    // F3 out
  votrax_real hF4in[4];  // F3 out + noise
  votrax_real hF4[3];    // F4 out
  votrax_real fxY;       // lowpass out
  votrax_real nX1, nX2;  // noise shaper input history
  votrax_real nY1, nY2;  // noise shaper output history
};

template <class Sink> void VotraxSc01::advance(uint32_t ticks, uint32_t tickHz, Sink &ring) {
  clkAcc += ticks * clockHz;
  uint32_t n = clkAcc / tickHz;
  clkAcc -= n * tickHz;
  if (idle && timerParam == T_NONE) {
    return;
  }

  while (n) {
    uint32_t step = n;
    if (timerParam != T_NONE && timerLeft < step) {
      step = timerLeft;
    }
    if (!idle) {
      const uint32_t toSample = SAMPLE_CLOCKS - sampleAcc;
      if (toSample < step) {
        step = toSample;
      }
      sampleAcc += step;
    }
    n -= step;
    if (timerParam != T_NONE) {
      timerLeft -= step;
      if (timerLeft == 0) {
        fireTimer();
      }
    }
    if (sampleAcc == SAMPLE_CLOCKS) {
      sampleAcc = 0;
      ring.push(sample());
    }
  }
}

#endif
