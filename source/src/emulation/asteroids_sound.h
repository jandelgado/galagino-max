#ifndef ASTEROIDS_SOUND_H
#define ASTEROIDS_SOUND_H

#include <stdint.h>

// Asteroids sound registers in machineBase::soundregs, written by the
// machine, read by the discrete sound renderer.
enum : uint8_t {
  AST_SND_LATCH = 0,       // audio latch 0x3c00-0x3c07, bit n = output n
  AST_SND_THUMP = 1,       // 0x3a00: b0-3 frequency, b4 enable
  AST_SND_EXPLODE = 2,     // 0x3600: b2-5 volume, b6-7 pitch
  AST_SND_NOISE_RESET = 3, // incremented on every 0x3e00 write
};

// audio latch outputs (MAME asteroid_sound)
enum : uint8_t {
  AST_LATCH_SAUCER = 0x01,
  AST_LATCH_SAUCER_FIRE = 0x02,
  AST_LATCH_SAUCER_LARGE = 0x04,
  AST_LATCH_THRUST = 0x08,
  AST_LATCH_SHIP_FIRE = 0x10,
  AST_LATCH_LIFE = 0x20,
};

// Asteroids discrete sound (MAME atari/asteroid_a.cpp, asteroid_discrete)
// as a small synthesizer: 24 kHz mono, output +-511 like the other
// renderers. Voice levels follow MAME's relative gain table.
class AsteroidsSound {
public:
  void reset();
  void render(const uint8_t *regs, int16_t *out, int n);

private:
  // first order low pass, y += a * (x - y)
  struct LowPass {
    float a, y;
    void set(float fc);
    float step(float x) { return y += a * (x - y); }
  };
  // MAME dst_filter2 band pass (bilinear, pre-warped)
  struct BandPass {
    float b0, a1, a2, x1, x2, y1, y2;
    void set(float fc, float damp);
    float step(float x);
  };
  // 555 based fire sound: falling frequency, decaying amplitude
  struct Fire {
    float f_start, f_end, f_step, amp, decay; // set()
    float f, env, phase;                      // running
    void set(float f_start, float f_end, float amp, float tau);
    float step(bool on);
  };

  float noise_step(uint8_t reset_count);
  float thump_step(uint8_t reg);
  float saucer_step(uint8_t latch);
  float explode_step(uint8_t reg, float noise);

  uint16_t lfsr;
  uint8_t noise_reset, noise_div;
  float noise;
  uint16_t thump_count;
  bool thump_high;
  float lfo_phase, saucer_phase;
  Fire ship_fire, saucer_fire;
  uint8_t explode_count;
  float explode_hold;
  uint8_t life_count;
  LowPass thump_lp, thrust_lp1, thrust_lp2, explode_lp;
  BandPass thrust_bp;
};

#endif
