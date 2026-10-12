#ifndef MARBLE_OCCLUSION_H
#define MARBLE_OCCLUSION_H

#include <stdint.h>
#include "marble_mem.h"

// Marble occlusion masks (MARBLE.md "Occlusion"), a 1:1 port of tools/marble_sim.py
// enter_1281c / occlusion_264aa / vertex_height_177f8.
//
//   enter(m, obj)   0x1281c: +0x1c = on screen, then the masks of a visible object
//   slinky(m, obj)  0x264aa mode 2: slinky masks at +0x2c, box of its current frame
//
// Results stay in the object image: +0x1c visible byte, +0x38 entries (5 x {code, x, y}
// big-endian words, first code 0 ends). Call after the projection (+0x20 is read).
namespace marble_occlusion {

void enter(marble_physics::Mem &m, uint32_t obj);
void slinky(marble_physics::Mem &m, uint32_t obj);

}  // namespace marble_occlusion

#endif
