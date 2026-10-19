#ifndef MARBLE_CAMERA_H
#define MARBLE_CAMERA_H

#include <stdint.h>
#include "marble_mem.h"

// Camera (MARBLE.md "Camera"). Y: port of 0x13ee6 from 0x1411c (scroll decision), state in
// the work RAM image at the original addresses. X: pan of the 240 px view over the 288 px
// playfield (remake only, the arcade screen shows the whole width).
//
//   start(m, race)       level init 0x16f6c: camera at the race top (silly: bottom)
//   tick(m)              per logic tick, before the player update (0x10fce)
//   top(m)               playfield y of the arcade screen's first line
//   pan(p, x)            per 60 Hz frame: keep playfield x between view x 64 and 176
namespace marble_camera {

void start(marble_physics::Mem &m, uint8_t race);
void tick(marble_physics::Mem &m);
int16_t top(const marble_physics::Mem &m);
int16_t range(const marble_physics::Mem &m);
int16_t pan(int16_t p, int16_t x);

}  // namespace marble_camera

#endif
