#ifndef MARBLE_DRAWLIST_H
#define MARBLE_DRAWLIST_H

#include <stdint.h>
#include "marble_mem.h"
#include "marble_video.h"

// MO draw list (MARBLE.md "Level object sprites"): a 1:1 port of the list upkeep of tools/
// marble_mo.py (DrawList, boxes_and_sort, publish). The arcade list lives in the work RAM
// image: list bytes 0x4003bc (0xff end, 31 max), entry k at 0x4001dc + 14 k {type, index,
// box x, y, z min, x, y, z max}. Back to front order, later is drawn on top.
//
//   init(m)                  0x28ca6: empty list, then the player (type 1)
//   insert(m, obj, type)     0x18e6c: object (slot or player), sorted into the list
//   remove(m, obj)           0x18f46: entry of the slot freed, list closed up
//   sort(m)                  0x26f3e head: boxes refreshed (0x1b12a), 3 sort passes (0x1a7a8)
//   build(...)               level object slots in list order as sprite objects
namespace marble_drawlist {

// out: at most max objects of the drawn slots, back to front. marbleAt: objects drawn before
// the player (count if it is not in the list). lift: tag 3 palette variant, LIFT_NONE if no
// lift frame is drawn. cam: camera word 0x40097e as the slot update saw it. Every arcade
// cull limit moves outward by cullMargin lines. Returns the count.
static const uint8_t LIFT_NONE = 0xff;
uint8_t build(const marble_physics::Mem &m, int16_t cam, int16_t cullMargin,
              marble_video::Obj *out, uint8_t max, uint8_t &marbleAt, uint8_t &lift);

void init(marble_physics::Mem &m);
void insert(marble_physics::Mem &m, uint32_t obj, uint8_t type);
void remove(marble_physics::Mem &m, uint32_t obj);  // type from obj +0x1f
void sort(marble_physics::Mem &m);

}  // namespace marble_drawlist

#endif
