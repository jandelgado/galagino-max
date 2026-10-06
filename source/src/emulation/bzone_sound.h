#ifndef BZONE_SOUND_H
#define BZONE_SOUND_H

#include <stdint.h>
#include "discrete.h"

// Battle Zone sound registers in machineBase::soundregs: POKEY registers
// first (pokey.h layout), then the raw sound latch byte (CPU 0x1840).
enum : uint8_t { BZ_SND_LATCH = 10 }; // after POKEY's 10 registers

// sound latch bits (MAME bzone_a.cpp)
enum : uint8_t {
  BZ_EXPLO = 0x01,      // explosion enable, gates the noise
  BZ_EXPLO_LOUD = 0x02, // explosion (and, in MAME, shell) loud
  BZ_SHELL = 0x04,      // shell enable
  BZ_SHELL_LOUD = 0x08, // unused by MAME's circuit
  BZ_ENGINE_REV = 0x10, // engine speed up
  BZ_SOUND_EN = 0x20,   // all sound on, POKEY included
  BZ_START_LED = 0x40,
  BZ_MOTOR_EN = 0x80,   // engine sound on
};

// Battle Zone discrete sound (MAME atari/bzone_a.cpp, bzone_discrete) with
// the POKEY output mixed in like on the board, 24 kHz mono, +-511.
//
//   noise LFSR (6 kHz) --/2--> RC gate (SHELL) --> filter --+
//                     \-NAND-/2--> RC gate (EXPLO) --> filter --+--> mixer --> DC block
//   CV (REV) -> 555 VCO -> /12, /10 counters (MOTOR) -> low pass --+     (SOUND_EN)
//   POKEY -----------------------------------------------------------+
class BzoneSound {
public:
  void reset();
  void render(uint8_t latch, const int16_t *pokey, int16_t *out, int n);

private:
  // MAME DISCRETE_RC_CIRCUIT_1: a 4066 gated RC, in0 charges, in1 passes
  struct RcCircuit {
    float vc, drop, exp1, exp2, exp12;
    void set(float r, float c);
    float step(bool in0, bool in1);
  };
  // MAME bzone_custom_filter: soft/loud gain, op-amp clamp, RC low pass
  struct Filter {
    float y, a, gain[2];
    void set(float r1, float r2, float r5, float c);
    float step(float in, bool loud);
  };

  void noise_clock();
  float engine_step(uint8_t latch);

  uint32_t sr;       // noise shift register, 17 bits
  uint8_t noise_div; // 24 kHz / 4 = 6 kHz
  bool noise, n31, n33, n34;
  RcCircuit shell_rc, explo_rc;
  Filter shell_f, explo_f;

  float vcv, rev_fast, rev_slow; // engine 555 control voltage, RC coefficients
  float t555, t_high, t_low;     // time in the current 555 phase, phase lengths
  bool high555;
  uint8_t f4, f5; // engine counters
  float engine_a, engine_y;
  float dc_a, dc_cap; // output coupling capacitor
};

#endif
