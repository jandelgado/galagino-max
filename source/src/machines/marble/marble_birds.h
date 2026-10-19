#ifndef MARBLE_BIRDS_H
#define MARBLE_BIRDS_H

#include <stdint.h>
#include "marble_mem.h"

// Silly race birds, internal part of marble_enemies (MARBLE.md "Birds in the main tick
// model"): 10 records of 0x38 bytes at 0x4019f8. Only marble_enemies.cpp includes this; the
// public entry points are in marble_enemies.h.
//
//   birdInit(m)                race start: bird numbers +0x19
//   birdBands(m, old, new)     band step, after the slot spawner: spawning flag 0x400762
//   birds_19baa(m)             per tick after the acid: spawns, all ten birds
namespace marble_enemies {

void birdInit(marble_physics::Mem &m);
void birdBands(marble_physics::Mem &m, int8_t oldBand, int8_t newBand);
void birds_19baa(marble_physics::Mem &m);

}  // namespace marble_enemies

#endif
