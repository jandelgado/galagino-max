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
//   build(m, cam, margin, v) objects of the drawn slots and of the player, the mask pool
namespace marble_drawlist {

// Fills v: objects back to front (n, at most OBJS), marbleAt (objects before the player),
// lift (tag 3 palette variant, LIFT_NONE if no lift frame is drawn), the mask pool. Player
// (0x2724c, type 1): the marble with its masks (marble, mask, nmask), or in states 1, 2, 4,
// 5, 9 objects at its list position: death frame +0x5a with the masks, sparkle stars, shatter
// overlay +0x62. Steelies (type 2), slinkies (type 4) and birds (type 0x0f) with their
// masks, acid (type 0x0e), minis (types 7-9), aerial pistons (type 0x29). cam: camera word 0x40097e as the slot update saw it. Every arcade cull limit
// moves outward by cullMargin lines. Returns the object count, also past OBJS.
static const uint8_t LIFT_NONE = 0xff;
uint8_t build(const marble_physics::Mem &m, int16_t cam, int16_t cullMargin,
              marble_video::VideoState &v);

void init(marble_physics::Mem &m);
void insert(marble_physics::Mem &m, uint32_t obj, uint8_t type);
void remove(marble_physics::Mem &m, uint32_t obj);  // type from obj +0x1f
void remove(marble_physics::Mem &m, uint32_t obj, uint8_t type);  // enemies (no +0x1f type)
void sort(marble_physics::Mem &m);

}  // namespace marble_drawlist

#endif
