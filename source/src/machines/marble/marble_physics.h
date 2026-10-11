#ifndef MARBLE_PHYSICS_H
#define MARBLE_PHYSICS_H

#include <stdint.h>
#include "marble_mem.h"

// Player marble update, a 1:1 port of the 68010 code (romconv/internal/marble/tools/
// marble_sim.py, MARBLE.md "Marble physics"). State lives in the work RAM image of Mem,
// at the original addresses, so the port stays bit exact with MAME traces.
//
//   startRace(m, race)        race start of player 1 on a cleared work RAM
//   tick(m, c6, c7) per 30 Hz logic tick:
//     physics 0x121b8 -> dizzy countdown -> new trackball deltas -> 0x253bc, input 0x17f66
//   project(m)                screen position 0x1b9cc to +0x1e/+0x20 (camera reads +0x20)
//   respawn(m)                after state 2/4: checkpoint search 0x262b2, reset 0x2591a
//
// Not ported (Phase 4): level object slots 0x29cce (surface code +0x58 is cleared),
// enemies and object collision, the death sequence (states 2/4), sprite dots, sounds.
namespace marble_physics {

static const uint32_t PLAYER = 0x400018;  // player 1 object
static const uint32_t CAMERA = 0x40097e;  // camera word (0x13ee6)

// player state +0x1a
enum State : uint8_t { ROLL = 0, DIZZY = 1, BROKEN = 2, LOST = 4, GOAL = 6 };

void startRace(Mem &m, uint8_t race);
void tick(Mem &m, int8_t c6, int8_t c7);
void respawn(Mem &m);

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
inline void project(Mem &m) {
  m.ww(PLAYER + 0x1e, (uint16_t)sx(m));
  m.ww(PLAYER + 0x20, (uint16_t)sy(m));
}

}  // namespace marble_physics

#endif
