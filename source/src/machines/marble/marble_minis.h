#ifndef MARBLE_MINIS_H
#define MARBLE_MINIS_H

#include <stdint.h>
#include "marble_mem.h"

// Silly race minis (small steelie, slinky, acid; bonus targets), internal part of
// marble_enemies (MARBLE.md "Minis in the main tick model"): 9 records of 0x28 bytes at
// 0x401890. Only marble_enemies.cpp includes this; the public entry points are in
// marble_enemies.h.
//
//   miniInit(m)                race start: mini numbers +0x19
//   miniBands(m, old, new)     band step, after the slot spawner: spawn / clear (0x18ffa,
//                              0x190ee)
//   minis_1912c(m)             per tick after the acid: all nine
namespace marble_enemies {

void miniInit(marble_physics::Mem &m);
void miniBands(marble_physics::Mem &m, int8_t oldBand, int8_t newBand);
void minis_1912c(marble_physics::Mem &m);

}  // namespace marble_enemies

#endif
