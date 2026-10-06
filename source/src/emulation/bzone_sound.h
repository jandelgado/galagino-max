#ifndef BZONE_SOUND_H
#define BZONE_SOUND_H

#include <stdint.h>

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

#endif
