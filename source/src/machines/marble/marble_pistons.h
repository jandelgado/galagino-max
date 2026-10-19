#ifndef MARBLE_PISTONS_H
#define MARBLE_PISTONS_H

#include <stdint.h>
#include "marble_mem.h"

// Aerial race pistons, internal part of marble_enemies (MARBLE.md "Pistons in the main tick
// model"): 36 entries of 0x10 bytes at 0x401650. Only marble_enemies.cpp includes this; the
// public entry points (arm_186ac, contact_1881c) are in marble_enemies.h.
//
//   pistons_1844a(m)   per tick after the birds: timers, rise / drop, screen positions
namespace marble_enemies {

void pistons_1844a(marble_physics::Mem &m);

}  // namespace marble_enemies

#endif
