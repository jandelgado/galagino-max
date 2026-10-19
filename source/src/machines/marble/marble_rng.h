#ifndef MARBLE_RNG_H
#define MARBLE_RNG_H

#include <stdint.h>
#include "marble_mem.h"

// Random numbers 0x13a98 (MARBLE.md "Player states, deaths, RNG"): 16 bit shift register in
// the work RAM image (word 0x4003a6), one step per significant bit of n.
//
//   rng(m, n)   0 .. n - 1; n = 0: the raw register after one step
//
// Consumers: race start 0x10fb8 and every logic tick 0x118a4 (rng(0x100)), random slot
// groups 0x12e54 (rng(4)), enemies (Phase 4b).
namespace marble_rng {

uint16_t rng(marble_physics::Mem &m, uint16_t n);

}  // namespace marble_rng

#endif
