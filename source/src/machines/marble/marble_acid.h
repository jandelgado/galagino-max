#ifndef MARBLE_ACID_H
#define MARBLE_ACID_H

#include <stdint.h>
#include "marble_mem.h"

// Acid (green puddles), internal part of marble_enemies (MARBLE.md "Acid in the main tick
// model"): 7 records of 0x42 bytes at 0x401482. Only marble_enemies.cpp includes this; the
// public entry points are in marble_enemies.h.
//
//   acidInit(m)                race start: acid numbers +0x19
//   acidBands(m, old, new)     band step: spawns/removals (0x17346)
//   acid_17230(m)              per tick after the slinkies: all seven (0x1725a each)
namespace marble_enemies {

void acidInit(marble_physics::Mem &m);
void acidBands(marble_physics::Mem &m, int8_t oldBand, int8_t newBand);
void acid_17230(marble_physics::Mem &m);

}  // namespace marble_enemies

#endif
