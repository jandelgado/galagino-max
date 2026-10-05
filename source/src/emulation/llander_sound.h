#ifndef LLANDER_SOUND_H
#define LLANDER_SOUND_H

#include <stdint.h>

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

#endif
