#include "marble_pistons.h"
#include "marble_enemies.h"
#include "marble_drawlist.h"
#include "marble_physics.h"
#include "marble_rng.h"
#include "marble_sound.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

// 1:1 port of dev/marble/marble_piston.py (function names as there; Marble Love state-sub-186ac.ts,
// read-abs-long-1844a.ts, state-sub-1881c.ts, sprite-coords.ts 0x18972, sprite-derive.ts
// 0x1bb28; the 0x1881c result and launch sign from the disassembly).
//
// In the aerial race a player entering zone 4/5 arms 36 pistons in three groups of pits.
// Each rises when its timer runs out, stays up for one animation (13 frames) and drops. A
// marble rolling onto a raised piston bounces off it; one on top of it as it rises is
// launched.

// everything in its own namespace: the host tests compile every module into one unit, where
// same-named constants of the other enemy files would hide these
namespace marble_pistons {

using marble_physics::Mem;
using marble_physics::s8;
using marble_physics::s16;
using marble_enemies::PISTONS;
using marble_enemies::PISTON_SIZE;
using marble_enemies::NPISTONS;

namespace {

// entry fields (MARBLE.md "Pistons in the main tick model")
enum : uint8_t {
  INDEX = 0x00,
  TIMER = 0x02,       // word: counts down; UP while risen
  CELL_X = 0x04, CELL_Y = 0x05,
  TOP = 0x06,         // z word of the risen top
  ANIM = 0x08,        // long: animation cursor
  SCREEN = 0x0c,      // sx, sy words
};

// player fields
enum : uint8_t { VX = 0x00, VY = 0x04, VZ = 0x08, PX = 0x0c, PY = 0x10, PZ = 0x14 };
enum : uint8_t { P_SLOT = 0x18, P_ZONE = 0x1b, P_AIR = 0x36 };

static const uint16_t UP = 0xffff;
static const uint8_t TYPE_PISTON = 0x29;  // draw type
static const uint8_t AERIAL = 3, LIVE = 1, AIRBORNE = 2;
static const uint8_t ARM_ZONE_A = 4, ARM_ZONE_B = 5;
static const uint8_t FIXED = 0x18;        // entries below use the fixed timer tables
static const uint8_t GROUP_A = 0xc, GROUP_B = 0x18, GROUPS = 3;  // sound groups (rng only)
static const uint8_t VARIANTS = 4;
static const int16_t WALL = 12;           // below top + 12 px the piston is a wall
static const uint32_t LAUNCH_VZ = 0x70000, LAUNCH_DZ = 0xc0000;
static const int32_t LAUNCH_DV = 0x6000;
static const uint32_t NO_FRAME = 0xffffffff;

// work RAM
static const uint32_t ARMED = 0x400760;   // byte
static const uint32_t TIMERS = 0x400764;  // long: timer table of entries 0x18-0x23
static const uint32_t PLAYERS = 0x400396;
static const uint32_t OLD_X = 0x400684, OLD_Y = 0x400688;   // longs
static const uint32_t CELL_XB = 0x400697, CELL_YB = 0x400699;  // low bytes of the cell words
static const uint32_t POS_X = 0x400690, POS_Y = 0x400692;    // words
static const uint16_t PLAYER_SIZE = 0xe2;

// program ROM
static const uint32_t ANIM_UP = 0x21342;
static const uint32_t TIMER_FIRST = 0x242c6, TIMER_RELOAD = 0x242f6;  // words, entries < 0x18
static const uint32_t VARIANT_FIRST = 0x243e6, VARIANT_RELOAD = 0x243f6;  // 4 longs each
static const uint32_t CELLS_X = 0x24406, CELLS_Y = 0x2442a, TOPS = 0x2444e;

uint32_t piston(uint8_t k) { return PISTONS + PISTON_SIZE * k; }

// The draw list reads an object's index at +0x19; a piston keeps it at +0.
uint32_t listObj(uint32_t e) { return e - 0x19; }

// timer of entry k: table word k (k < 0x18), else word k - 0x18 of the variant table
uint16_t timerWord(const Mem &m, uint8_t k, uint32_t fixed) {
  return k < FIXED ? m.romw(fixed + 2 * k) : m.romw(m.rl(TIMERS) + 2 * (k - FIXED));
}

// 0x18972: screen position +0xc from the cell (* 8) and the top (0x400690/2 written)
void coords_18972(Mem &m, uint32_t e) {
  const uint16_t x = (uint16_t)(s8(m.rb(e + CELL_X)) * 8), y = (uint16_t)(s8(m.rb(e + CELL_Y)) * 8);
  m.ww(POS_X, x);
  m.ww(POS_Y, y);
  const uint16_t sx = (uint16_t)(y - x + 0x88);
  const uint16_t sy = (uint16_t)(m.rw(marble_physics::CAMERA) + m.rw(e + TOP) + 0x54 -
                                 (uint16_t)(((int32_t)s16(x) + s16(y)) >> 1));
  m.wl(e + SCREEN, (uint32_t)sx << 16 | sy);
}

bool armedPlayer(const Mem &m) {
  bool armed = false;
  for (uint16_t k = 0; k < m.rw(PLAYERS); k++) {
    const uint32_t p = marble_physics::PLAYER + PLAYER_SIZE * k;
    const uint8_t z = m.rb(p + P_ZONE);
    if (m.rb(p + P_SLOT) == LIVE && (z == ARM_ZONE_A || z == ARM_ZONE_B)) { armed = true; }
  }
  return armed;
}

// 0x186ac init: variant rng(4), every entry's timer, cell, top; 0x1bb28 per entry leaves the
// last cell in the scratch
void arm(Mem &m) {
  const uint8_t v = (uint8_t)marble_rng::rng(m, VARIANTS);
  m.wl(TIMERS, m.roml(VARIANT_FIRST + 4 * v));
  for (uint8_t k = 0; k < NPISTONS; k++) {
    const uint32_t e = piston(k);
    m.wb(e + INDEX, k);
    m.ww(e + TIMER, timerWord(m, k, TIMER_FIRST));
    m.wb(e + CELL_X, m.romb(CELLS_X + k));
    m.wb(e + CELL_Y, m.romb(CELLS_Y + k));
    marble_physics::cellScratch(m, (int16_t)(s8(m.rb(e + CELL_X)) * 8),
                                (int16_t)(s8(m.rb(e + CELL_Y)) * 8));
    m.ww(e + TOP, m.romw(TOPS + 2 * k));
  }
  m.wl(TIMERS, m.roml(VARIANT_RELOAD + 4 * v));
  m.wb(ARMED, 1);
}

// 0x186ac teardown: pistons up leave the draw list; timer, cell, top cleared
void teardown(Mem &m) {
  for (uint8_t k = 0; k < NPISTONS; k++) {
    const uint32_t e = piston(k);
    if (m.rw(e + TIMER) == UP) { marble_drawlist::remove(m, listObj(e), TYPE_PISTON); }
    m.ww(e + TOP, 0);
    m.wb(e + CELL_Y, 0);
    m.wb(e + CELL_X, 0);
    m.ww(e + TIMER, 0);
  }
  m.wb(ARMED, 0);
}

uint8_t group(uint8_t k) { return k < GROUP_A ? 0 : (k < GROUP_B ? 1 : 2); }

}  // namespace

// 0x186ac (aerial race, from the zone hook 0x13966): a live player in zone 4/5 arms the
// table, none tears it down
void arm_186ac(Mem &m) {
  if (m.rw(marble_physics::RACE) != AERIAL) { return; }
  const bool want = armedPlayer(m);
  if (!m.rb(ARMED) && want) {
    arm(m);
  } else if (m.rb(ARMED) && !want) {
    teardown(m);
  }
}

// 0x1844a (armed aerial race): timers count down; at 0 the piston rises (anim, draw list); up,
// its anim steps and at the end it drops (draw list, timer reload). One rng(2) per group that
// rose (sound pick, sounds not ported).
void update_1844a(Mem &m) {
  if (m.rw(marble_physics::RACE) != AERIAL || !m.rb(ARMED)) { return; }

  bool rose[GROUPS] = {false, false, false};
  for (uint8_t k = 0; k < NPISTONS; k++) {
    const uint32_t e = piston(k);
    const uint16_t t = m.rw(e + TIMER);
    if (s16(t) >= 0) {
      const uint16_t left = (uint16_t)(t - 1);
      m.ww(e + TIMER, left);
      if (s16(left) <= 0) {
        m.wl(e + ANIM, ANIM_UP);
        m.ww(e + TIMER, UP);
        marble_drawlist::insert(m, listObj(e), TYPE_PISTON);
        rose[group(k)] = true;
      }
    } else if (t == UP) {
      m.wl(e + ANIM, m.rl(e + ANIM) + 4);
      if (m.roml(m.rl(e + ANIM)) == NO_FRAME) {
        marble_drawlist::remove(m, listObj(e), TYPE_PISTON);
        m.ww(e + TIMER, timerWord(m, m.rb(e + INDEX), TIMER_RELOAD));
      }
    }
    if (m.rw(e + TIMER) == UP) { coords_18972(m, e); }
  }
  for (bool r : rose) {
    if (r) { marble_rng::rng(m, 2); }
  }
}

// 0x1881c (armed aerial race, players): every piston up on the new cell sends a2 back to its
// old position; from its own cell on the top it is launched, lower than top + 12 px it
// bounces. True if any piston matched (the caller refreshes the cell).
bool contact_1881c(Mem &m, uint32_t a2) {
  if (m.rw(marble_physics::RACE) != AERIAL || !m.rb(ARMED)) { return false; }

  const uint8_t nx = m.rb(CELL_XB), ny = m.rb(CELL_YB);
  const uint8_t ox = (uint8_t)(m.sl(OLD_X) >> 19), oy = (uint8_t)(m.sl(OLD_Y) >> 19);
  bool hit = false;
  for (uint8_t k = 0; k < NPISTONS; k++) {
    const uint32_t e = piston(k);
    if (m.rw(e + TIMER) != UP || m.rb(e + CELL_X) != nx || m.rb(e + CELL_Y) != ny) { continue; }

    hit = true;
    m.wl(a2 + PX, m.rl(OLD_X));
    m.wl(a2 + PY, m.rl(OLD_Y));
    if (m.rb(e + CELL_X) == ox && m.rb(e + CELL_Y) == oy && m.rw(e + TOP) == m.rw(a2 + PZ)) {
      // 0x13a98 writes only the low word of d0 and 0x1881c tests the long: the high word
      // left from before decides too (first draw 0xffff: always plus, second: the new vx
      // high word)
      m.wl(a2 + VZ, LAUNCH_VZ);
      m.wl(a2 + PZ, m.rl(a2 + PZ) + LAUNCH_DZ);
      uint16_t hi = 0xffff;
      for (uint8_t o = VX; o <= VY; o += 4) {
        const bool plus = marble_rng::rng(m, 2) != 0 || hi != 0;
        m.wl(a2 + o, (uint32_t)((m.sl(a2 + o) >> 1) + (plus ? LAUNCH_DV : -LAUNCH_DV)));
        hi = m.rw(a2 + o);
      }
      m.wb(a2 + P_AIR, AIRBORNE);
      marble_sound::send(m, marble_sound::SND_LAUNCH);
    } else if (s16(m.rw(a2 + PZ)) - s16(m.rw(e + TOP)) < WALL) {
      m.wl(a2 + VX, 0u - m.rl(a2 + VX));
      m.wl(a2 + VY, 0u - m.rl(a2 + VY));
    }
  }
  return hit;
}

}  // namespace marble_pistons

namespace marble_enemies {

using marble_physics::Mem;

void pistons_1844a(Mem &m) { marble_pistons::update_1844a(m); }
void arm_186ac(Mem &m) { marble_pistons::arm_186ac(m); }
bool contact_1881c(Mem &m, uint32_t a2) { return marble_pistons::contact_1881c(m, a2); }

}  // namespace marble_enemies

#endif  // GAME_MARBLE
