#ifndef LLANDER_SOUND_H
#define LLANDER_SOUND_H

#include <stdint.h>
#include "discrete.h"

// Lunar Lander sound registers in machineBase::soundregs
enum : uint8_t {
  LL_SND_CTRL = 0,        // 0x3c00 value
  LL_SND_NOISE_RESET = 1, // incremented on every 0x3e00 write
};

// LL_SND_CTRL bits (MAME llander_sounds_w)
enum : uint8_t {
  LL_THRUST_MASK = 0x07, // thrust volume
  LL_EXPLODE = 0x08,
  LL_TONE_3K = 0x10,
  LL_TONE_6K = 0x20,
};

// Lunar Lander discrete sound (MAME atari/asteroid.cpp, llander_discrete)
// as a small synthesizer: 24 kHz mono, output +-511 like the other
// renderers. Thrust and explosion levels follow MAME's relative gain
// table; the beeps get their own level, see TONE_LEVEL.
class LlanderSound {
public:
  void reset();
  void render(const uint8_t *regs, int16_t *out, int n);

private:
  discrete::Noise noise;
  discrete::LowPass noise_rc, out_lp;
  discrete::BandPass thrust_bp;
  uint8_t tone_count;
};

#endif
