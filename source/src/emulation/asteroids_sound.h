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

#endif
