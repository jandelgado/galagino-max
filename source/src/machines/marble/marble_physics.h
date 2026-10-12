#ifndef MARBLE_PHYSICS_H
#define MARBLE_PHYSICS_H

#include <stdint.h>
#include "marble_mem.h"

// Player marble update, a 1:1 port of the 68010 code (romconv/internal/marble/tools/
// marble_sim.py, MARBLE.md "Marble physics"). State lives in the work RAM image of Mem,
// at the original addresses, so the port stays bit exact with MAME traces.
//
//   startRace(m, race)        race start of player 1 on a cleared work RAM, level objects,
//                             time out byte rng(0x100) (0x10fb8)
//   tick(m, c6, c7) per 30 Hz logic tick (0x251de): trackball deltas of the tick to
//     +0xc6/+0xc7, then the handler of state +0x1a (0x253ec):
//       ROLL    0x253bc, input 0x17f66, physics 0x121b8
//       DIZZY   animation 0x25fc2, as ROLL, countdown
//       TUBE    dropped out of a silly race pipe exit (0x25514): waits, scores, then ROLL
//       BROKEN  shatter animation 0x25fc2, projection, masks; ends in LOST
//       LOST    respawned at entry (0x2591a); sparkle 0x13ade, then ROLL
//       EATEN   held by a slinky: animation 0x25fc2, as ROLL, countdown
//       GOAL    physics, countdown, then out of play
//       HIDDEN  inside a slinky (0x25812) until it sends the player LOST
//       DISSOLVING  in acid (0x2584e): slides into it, dissolve animation, then LOST
//       CARRIED  carried off by a bird (0x25876): orbit, then LOST
//   project(m)                screen position 0x1b9cc of the player. The physics tail calls
//                             it; ticks that return early keep the stored value, as in the
//                             arcade. Call it where the physics does not run.
//
// Level object slots (marble_objects.h): 0x29cce runs inside the physics, startRace spawns
// the first slots. Steelies (marble_enemies.h) run the same object physics: pair collision
// 0x1bc88, non-player landing and loss go to marble_enemies; the player's physics also runs
// the slinky contact 0x14e92, the acid contact 0x175c8, the piston contact 0x1881c, the mini
// contact 0x1924e and the bird contact 0x19d94; zone changes and respawns run the zone hook
// 0x13966 (pistons 0x186ac, zone wait slots). Not ported (Phase 4): states 8, 0xa, the race
// timer (time out 0x1bbaa), sprite dots 0x1c014, sounds.
namespace marble_physics {

static const uint32_t PLAYER = 0x400018;  // player 1 object

// player state +0x1a (handler table 0x254ba)
enum State : uint8_t {
  ROLL = 0, DIZZY = 1, BROKEN = 2, TUBE = 3, LOST = 4, EATEN = 5, GOAL = 6, HIDDEN = 7,
  DISSOLVING = 9,
  CARRIED = 0xb
};
static const uint8_t CARRIED_DAMAGE = 0x66;  // +0x57 a bird sets: orbit length of CARRIED

void startRace(Mem &m, uint8_t race);
void tick(Mem &m, int8_t c6, int8_t c7);

// position, 16.16 pixels; z high word = height
inline int32_t x(const Mem &m) { return m.sl(PLAYER + 0x0c); }
inline int32_t y(const Mem &m) { return m.sl(PLAYER + 0x10); }
inline int32_t z(const Mem &m) { return m.sl(PLAYER + 0x14); }
inline uint8_t state(const Mem &m) { return m.rb(PLAYER + 0x1a); }

// Projection 0x1b9cc (MARBLE.md "Marble sprite"); sy counts up from the screen bottom.
// sy: x + y summed as longs, asr 1, word result. Surface codes 0x2f-0x31 (bumps) add 2 on
// odd x when |vx| > 0x4000.
inline int16_t sx(const Mem &m) {
  return (int16_t)(m.rw(PLAYER + 0x10) - m.rw(PLAYER + 0x0c) + 0x88);
}
inline int16_t sy(const Mem &m) {
  uint16_t z = m.rw(PLAYER + 0x14);
  const uint8_t s = m.rb(PLAYER + 0x58);
  if (s >= 0x2f && s <= 0x31 && (m.rb(PLAYER + 0x0d) & 1)) {
    const uint32_t vx = m.rl(PLAYER + 0x00);
    if ((int32_t)((int32_t)vx < 0 ? 0u - vx : vx) > 0x4000) { z += 2; }  // neg.l, cmpi.l
  }
  const int32_t xy = (int32_t)m.sw(PLAYER + 0x0c) + m.sw(PLAYER + 0x10);
  return (int16_t)(m.rw(CAMERA) + z + 0x54 - (uint16_t)(xy >> 1));
}

// 0x1b9cc: +0x1e/+0x20 (camera reads +0x20), older pairs +0x22, +0x26. A position equal to
// one of the stored pairs is not written. REFETCH reloads the terrain cell first.
enum Refetch : uint8_t { KEEP_CELL, REFETCH };
void project(Mem &m);
void project(Mem &m, uint32_t obj, Refetch r);

// Object helpers for the enemies (marble_enemies.cpp), ports named by ROM address:
//   isPlayer(obj)            player 1 or 2 object
//   move(m, obj)             0x121b8 object physics (players: zones, gravity too)
//   ground(m, obj)           0x253bc ground surface code
//   speedCap(m, obj)         0x26196 speed cap (players: distance score 0x26214)
//   floorZ(m, obj)           0x1bab2 with the cell refetched, then 0x1cc62: floor z
//   cellHeight(m, obj)       0x1bb08 + 0x1cc62(1): x/y words to the cell scratch, cell
//                            fetched, floor z (the z word scratch is left alone)
//   cellScratch(m, x, y)     0x1bb28: pixel x/y words to the cell scratch, cell and fraction
//                            derived (0x1bb50), no fetch
//   drag(m, obj)             0x25e7c mode 2: velocity friction of a pushed slinky
//   enterState(m, obj, s)    0x25bae state entry (BROKEN, LOST, DISSOLVING)
//   blocked(m, obj, x, y, r) 0x17cb8: another live object near pixel (x, y), 0 if none
//   animate(m, obj)          0x25fc2 animation step of +0x5a; a shattering steelie leaves
//                            the draw list at the end
//   score(m, player, k)      0x285b0 points entry k
bool isPlayer(uint32_t obj);
void move(Mem &m, uint32_t obj);
void ground(Mem &m, uint32_t obj);
void speedCap(Mem &m, uint32_t obj);
int32_t floorZ(Mem &m, uint32_t obj);
int32_t cellHeight(Mem &m, uint32_t obj);
void cellScratch(Mem &m, int16_t x, int16_t y);
void drag(Mem &m, uint32_t obj);
void enterState(Mem &m, uint32_t obj, State s);
uint8_t blocked(Mem &m, uint32_t obj, int16_t x, int16_t y, uint16_t r);
void animate(Mem &m, uint32_t obj);
void score(Mem &m, uint32_t player, uint8_t k);

}  // namespace marble_physics

#endif
