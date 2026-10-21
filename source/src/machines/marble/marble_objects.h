#ifndef MARBLE_OBJECTS_H
#define MARBLE_OBJECTS_H

#include <stdint.h>
#include "marble_mem.h"

// Level object slots (MARBLE.md "Level objects"): 25 slots of 0x56 bytes at 0x400a9c in the
// work RAM image, a 1:1 port of dev/marble/marble_sim.py with objects on. Slots are spawned by
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
//   zoneScripts(m, zone)          zone hook 0x13966 slot part: zone wait slots of the zone
//                                 restart their script
//   zone4_13892(m)                aerial zone 4 entry: zone wait slots removed, open gates
//                                 closed again
//   closeGate(m, slot)            a gate that caught the marble (slot number) closes:
//                                 close script restarted (end of player state 0xa)
//
// Per logic tick: applyQueued, camera tick + cameraBands, physics::tick, update.
// Not ported: two-player catapult wait.
namespace marble_objects {

void init(marble_physics::Mem &m, uint8_t race);
void cameraBands(marble_physics::Mem &m, int16_t oldCam, int16_t newCam);
void applyQueued(marble_physics::Mem &m);
void update(marble_physics::Mem &m);
void slots_29cce(marble_physics::Mem &m, uint32_t a2);
uint32_t lastPatch(const marble_physics::Mem &m);
void zoneScripts(marble_physics::Mem &m, uint8_t zone);
void zone4_13892(marble_physics::Mem &m);
void closeGate(marble_physics::Mem &m, uint8_t slot);

}  // namespace marble_objects

#endif
