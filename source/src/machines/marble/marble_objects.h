#ifndef MARBLE_OBJECTS_H
#define MARBLE_OBJECTS_H

#include <stdint.h>
#include "marble_mem.h"

// Level object slots (MARBLE.md "Level objects"): 25 slots of 0x56 bytes at 0x400a9c in the
// work RAM image, a 1:1 port of tools/marble_sim.py with objects on. Slots are spawned by
// camera band changes, run a byte code script (walls, gates, animated terrain) and collide
// with the player marble.
//
//   init(m, race)                 level init 0x105a0-0x10614 (slot part)
//   cameraBands(m, old, new)      camera word changed: spawn / remove steelies, then slots
//                                 (0x144e4); call with the old camera still stored
//   applyQueued(m)                queued animation frame: dynamic terrain codes (0x1344c)
//   update(m)                     per logic tick slot update (0x12fd0)
//   slots_29cce(m, a2)            object a2 against the slots, from the physics (0x29cce)
//   lastPatch(m)                  record of the last playfield patch (mode 1 slots), 0 = none
//
// Per logic tick: applyQueued, camera tick + cameraBands, physics::tick, update.
// Not ported (Phase 4): tags 0x0a-0x0d, 0x12-0x16, 0x20, 0x21, 0x25-0x27 (tubes, gates,
// catapult): their contact is ignored; spawners of enemies other than steelies; MO graphics of moving parts.
namespace marble_objects {

void init(marble_physics::Mem &m, uint8_t race);
void cameraBands(marble_physics::Mem &m, int16_t oldCam, int16_t newCam);
void applyQueued(marble_physics::Mem &m);
void update(marble_physics::Mem &m);
void slots_29cce(marble_physics::Mem &m, uint32_t a2);
uint32_t lastPatch(const marble_physics::Mem &m);

}  // namespace marble_objects

#endif
