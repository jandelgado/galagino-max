#ifndef MARBLE_PARTICLES_H
#define MARBLE_PARTICLES_H

#include <stdint.h>
#include "marble_mem.h"
#include "marble_video.h"

// Bouncing marbles of the title and the final screen (MARBLE.md "Attract"; model
// tools/marble_attract.py): slots 0x400a9c (x, y in 1/16 px, vx, vy, mode word) in the work
// RAM image, count 0x4003e2. The emulation core inits and bounces them, publish turns them
// into view positions for the renderer.
//
//   init(m, count, mode)   0x18cd2 (mode 0xff: palette fill 0x26cfa first), returns the
//                          fill variant bits (bit k: colour set k)
//   bounce(m)              0x18dca, one 60 Hz frame
//   publish(m, mode, fill, v)  parts / nparts of v (objects cleared)
namespace marble_particles {

static const uint8_t TITLE_COUNT = 3, FINAL_COUNT = 28;
static const uint8_t TITLE_MODE = 0x80;  // rng(2) colour per marble
static const uint8_t FINAL_MODE = 0xff;  // rng(8) colour, palette fill first

uint8_t init(marble_physics::Mem &m, uint8_t count, uint8_t mode);
void bounce(marble_physics::Mem &m);
void publish(const marble_physics::Mem &m, uint8_t mode, uint8_t fill,
             marble_video::VideoState &v);
inline uint8_t count(const marble_physics::Mem &m) { return m.rb(0x4003e2); }
void clear(marble_physics::Mem &m);

}  // namespace marble_particles

#endif
