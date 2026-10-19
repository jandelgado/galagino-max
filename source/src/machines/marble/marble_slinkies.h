#ifndef MARBLE_SLINKIES_H
#define MARBLE_SLINKIES_H

#include <stdint.h>
#include "marble_mem.h"

// Slinkies (green worms), internal part of marble_enemies (MARBLE.md "Slinkies in the main
// tick model"): 4 records of 0x60 bytes at 0x401302. Only marble_enemies.cpp includes this;
// the public entry points are in marble_enemies.h.
//
//   slinkyInit(m)              race start: slinky numbers +0x19
//   slinkyBands(m, old, new)   band step: spawns/removals (0x14c46)
//   slinkies_1493c(m)          per tick after the steelies: all four (0x14966 each)
namespace marble_enemies {

void slinkyInit(marble_physics::Mem &m);
void slinkyBands(marble_physics::Mem &m, int8_t oldBand, int8_t newBand);
void slinkies_1493c(marble_physics::Mem &m);

}  // namespace marble_enemies

#endif
