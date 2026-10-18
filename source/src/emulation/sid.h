#ifndef SID_H
#define SID_H

#include <stdint.h>

// MOS 6581 SID sound part, port of the Rockbox TinySID synth (GPLv2+,
// Tammo Hinrichs and Rainer Sinsch 1998-1999, integer version 2006).
// Integer only, fixed 24 kHz. Cheap, not exact: linear envelopes,
// AND-combined waveforms, 8580-like filter that saturates near 6 kHz.
class Sid {
public:
  static const uint32_t SAMPLE_RATE = 24000;
  static const uint8_t REG_COUNT = 0x19; // writable $D400..$D418
  static const int32_t SAMPLE_MAX = 480;

  void reset();
  // reg = address - $D400; read-only and unused registers are ignored
  void write(uint8_t reg, uint8_t val);
  // n samples centered on 0, +-SAMPLE_MAX with all voices at full level
  void render(int16_t *out, int n);

private:
  struct Voice { // register values
    uint16_t freq, pulse;
    uint8_t ctrl, ad, sr;
  };
  struct Osc { // oscillator + envelope state
    uint32_t counter;
    int32_t env;
    uint32_t noisepos, noiseval;
    uint8_t envphase, noiseout;
  };

  Voice voice[3];
  Osc osc[3];
  uint8_t fc_lo, fc_hi, res_filt, mode_vol;
  int32_t flt_h, flt_b, flt_l; // 16.16 filter state
};

#endif
