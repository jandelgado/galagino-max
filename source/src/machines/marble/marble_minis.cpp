#include "marble_minis.h"
#include "marble_enemies.h"
#include "marble_drawlist.h"
#include "marble_physics.h"
#include "marble_rng.h"

#ifdef ENABLE_MARBLE

// 1:1 port of tools/marble_mini.py (function names as there; Marble Love
// array-9-init-and-dispatch-18ffa.ts, array-9-clear-and-dispatch.ts, refresh-helper-1912c.ts,
// object-type-dispatch-194ba.ts, state-sub-1960e.ts, sub-19692.ts, state-sub-198bc.ts,
// sub-19976.ts, sub-1937c.ts, helper-1924e.ts; 0x1973c and 0x1953e from the disassembly).
//
// While the camera band is in 0x1d-0x38, nine minis wander a fenced field by the silly race
// start. A player on the field floor makes the steelies flee and the others follow; touching
// one collects it (points, race time).

// everything in its own namespace: the host tests compile every module into one unit, where
// same-named constants of the other enemy files would hide these
namespace marble_minis {

using marble_physics::Mem;
using marble_physics::s8;
using marble_physics::s16;
using marble_enemies::MINIS;
using marble_enemies::MINI_SIZE;
using marble_enemies::NMINIS;

namespace {

// mini fields (MARBLE.md "Minis in the main tick model")
enum : uint8_t {
  VX = 0x00, VY = 0x04, VZ = 0x08, PX = 0x0c, PY = 0x10, PZ = 0x14,
  SLOT = 0x18, IDX = 0x19, KIND = 0x1a, STEPS = 0x1b,
  ANIM = 0x1c,                     // long: animation cursor
  SCREEN = 0x20,                   // sx, sy words
  TICK = 0x24, TYPE = 0x25, DIR = 0x26,
};
enum : uint8_t { WANDER = 0, NEAR = 1, COLLECTED = 2 };   // +0x1a
enum : uint8_t { STEELIE = 7, SLINKY = 8, ACID = 9 };     // +0x25 = draw type

// player fields
enum : uint8_t { P_SLOT = 0x18, P_ZONE = 0x1b, P_TIME = 0x6a, P_SCORE = 0xbc };

static const uint8_t LIVE = 1, SILLY = 4, FIELD_ZONE = 1;
static const uint16_t FIELD_FLOOR = 0x3f6e;           // player z word that makes minis NEAR
static const uint32_t Z0 = 0x3f6e0000;
static const int8_t BAND_LO = 0x1d, BAND_HI = 0x38;   // mini band (0x144e4)
static const uint8_t STUCK = 0x10, DIRS = 0xf;
static const uint8_t TRIES_WANDER = 12, TRIES_CHASE = 9;
static const int16_t NEAR_PX = 0xc;                   // 0x1937c: other mini closer on both axes
static const int16_t HIT_PX = 4, HIT_PX_STEELIE = 5;  // 0x1924e
static const uint16_t TIME_BONUS = 3;                 // race seconds
static const uint8_t POINTS = 0xe;                    // 0x285b0 entry
static const int32_t POINTS_HIT = 500;                // 0x28608
static const uint16_t FAR = 0x140;                    // 0x1973c: player search radius
static const uint8_t SPAWN_RNG = 0x20;
static const uint16_t FIELD_X = 0x2ca, FIELD_Y = 0x2d2;

// work RAM
static const uint32_t PLAYERS = 0x400396;
static const uint32_t POS_X = 0x400690, POS_Y = 0x400692;  // words
static const uint32_t SCORED = 0x40039c;
static const uint16_t PLAYER_SIZE = 0xe2;

// program ROM
static const uint32_t STEP_X = 0x244b6, STEP_Y = 0x244d6;  // words per direction (<< 8)
static const uint32_t GRID = 0x24496;                      // 16 row words, set = solid
static const uint8_t GRID_X0 = 0x59, GRID_Y0 = 0x5a, GRID_N = 0xf;
static const uint32_t NO_FRAME = 0xffffffff;
static const uint32_t WALK_STEELIE = 0x21f72;
// 0x1953e walk by v: y > 0, y < 0, x > 0, x < 0, still
static const uint32_t WALK_SLINKY[5] = {0x219b2, 0x21a32, 0x2197a, 0x219ea, 0x2194e};
static const uint32_t WALK_ACID[5] = {0x21e3e, 0x21eb6, 0x21d4e, 0x21dc6, 0x21f06};
static const uint32_t GONE_STEELIE = 0x21f8a, GONE_SLINKY = 0x21a62, GONE_OTHER = 0x21efe;
static const uint32_t HIT_ACID = 0x21efe, HIT_STEELIE = 0x21f7a;

uint32_t mini(uint8_t i) { return MINIS + MINI_SIZE * i; }

int16_t absw(int16_t v) { return v < 0 ? (int16_t)-v : v; }

bool inBand(int8_t b) { return BAND_LO <= b && b <= BAND_HI; }

// 0x199d6: screen position +0x20 as slot objects
void coords_199d6(Mem &m, uint32_t a2) {
  const uint16_t x = m.rw(a2 + PX), y = m.rw(a2 + PY);
  m.ww(POS_X, x);
  m.ww(POS_Y, y);
  const uint16_t sx = (uint16_t)(y - x + 0x88);
  const uint16_t sy = (uint16_t)(m.rw(marble_physics::CAMERA) + m.rw(a2 + PZ) + 0x54 -
                                 (uint16_t)(((int32_t)s16(x) + s16(y)) >> 1));
  m.wl(a2 + SCREEN, (uint32_t)sx << 16 | sy);
}

// 0x1937c: another live mini (not collected) within 12 px on both axes, or a solid cell of
// the field bitmap (outside its window counts as solid)
bool blocked_1937c(const Mem &m, uint32_t a2) {
  const int16_t x = m.sw(a2 + PX), y = m.sw(a2 + PY);
  for (uint8_t i = 0; i < NMINIS; i++) {
    const uint32_t o = mini(i);
    if (o == a2 || !m.rb(o + SLOT) || m.rb(o + KIND) == COLLECTED) { continue; }
    if (absw(s16((uint32_t)(m.sw(o + PX) - x))) < NEAR_PX &&
        absw(s16((uint32_t)(m.sw(o + PY) - y))) < NEAR_PX) {
      return true;
    }
  }
  const int8_t cx = s8((uint32_t)((x >> 3) - GRID_X0)), cy = s8((uint32_t)((y >> 3) - GRID_Y0));
  if (cx < 0 || cx > GRID_N || cy < 0 || cy > GRID_N) { return true; }
  return (m.romw(GRID + 2 * cy) & (1u << cx)) != 0;
}

// 0x19976: one step in direction +0x26; v = the step (steelie: / 4)
void step_19976(Mem &m, uint32_t a2) {
  const int8_t d = m.sb(a2 + DIR);
  int32_t dx = s16(m.romw(STEP_X + 2 * d)) * 256, dy = s16(m.romw(STEP_Y + 2 * d)) * 256;
  m.wl(a2 + PX, m.rl(a2 + PX) + (uint32_t)dx);
  m.wl(a2 + PY, m.rl(a2 + PY) + (uint32_t)dy);
  if (m.rb(a2 + TYPE) == STEELIE) {
    dx >>= 2;
    dy >>= 2;
  }
  m.wl(a2 + VX, (uint32_t)dx);
  m.wl(a2 + VY, (uint32_t)dy);
}

enum Back : uint8_t { ANY_DIR, SKIP_BACK };

// 0x19692 (12 tries) / 0x198bc (9 tries, SKIP_BACK): a free step in direction +0x26 sets v;
// blocked, the direction turns by 1 (steelie) or 4 (every fourth try); none free: stuck, v 0.
// The position is restored in every case.
void turn(Mem &m, uint32_t a2, uint8_t tries, Back back) {
  if (m.rb(a2 + DIR) == STUCK) { return; }

  const uint32_t x0 = m.rl(a2 + PX), y0 = m.rl(a2 + PY);
  const bool fine = m.rb(a2 + TYPE) == STEELIE;
  const uint8_t step = fine ? 1 : 4;
  auto restore = [&]() {
    m.wl(a2 + PX, x0);
    m.wl(a2 + PY, y0);
  };

  step_19976(m, a2);
  if (!blocked_1937c(m, a2)) {
    restore();
    return;
  }
  const uint8_t d4 = m.rb(a2 + DIR);
  if (back == SKIP_BACK) { m.wb(a2 + DIR, (uint8_t)(d4 - 4)); }
  for (uint8_t k = 0; k < tries; k++) {
    if (!fine && (k & 3)) { continue; }
    restore();
    m.wb(a2 + DIR, (m.rb(a2 + DIR) + step) & DIRS);
    if (back == SKIP_BACK && m.rb(a2 + DIR) == d4) { continue; }
    step_19976(m, a2);
    if (!blocked_1937c(m, a2)) {
      restore();
      return;
    }
  }
  m.wb(a2 + DIR, STUCK);
  m.wl(a2 + VY, 0);
  m.wl(a2 + VX, 0);
  restore();
}

// 0x1960e: steelie jitters its direction by -2..2; the others pick one (axis by v, side by
// rng(2)), an acid stops 1 in 4; then 0x19692
void wander_1960e(Mem &m, uint32_t a2) {
  if (m.rb(a2 + TYPE) == STEELIE) {
    const uint8_t r = (uint8_t)marble_rng::rng(m, 5);
    m.wb(a2 + DIR, (uint8_t)(m.rb(a2 + DIR) + r - 2) & DIRS);
  } else {
    const uint8_t r = (uint8_t)marble_rng::rng(m, 2);
    m.wb(a2 + DIR, (uint8_t)((r << 3) + (m.rl(a2 + VX) ? 4 : 0)));
    if (marble_rng::rng(m, 4) == 0 && m.rb(a2 + TYPE) == ACID) {
      m.wb(a2 + DIR, STUCK);
      m.wl(a2 + VY, 0);
      m.wl(a2 + VX, 0);
    }
  }
  turn(m, a2, TRIES_WANDER, ANY_DIR);
}

// octant distance (max + 3/8 min of the 16x deltas, words)
uint16_t octant(uint16_t ax, uint16_t ay) {
  return ax >= ay ? (uint16_t)((ay >> 3) * 3 + ax) : (uint16_t)((ax >> 3) * 3 + ay);
}

// 0x1973c (disassembly): nearest player below FAR (else player 1); steelie: direction away
// from it (16ths by the axis ratio and quadrant), the others one axis towards it; 0x198bc
void chase_1973c(Mem &m, uint32_t a2) {
  uint16_t best = FAR;
  uint32_t p = marble_physics::PLAYER;
  for (uint16_t k = 0; k < m.rw(PLAYERS); k++) {
    const uint32_t a = marble_physics::PLAYER + PLAYER_SIZE * k;
    if (!m.rb(a + P_SLOT)) { continue; }
    const uint16_t ax = (uint16_t)(absw(s16((uint32_t)(m.rw(a2 + PX) - m.rw(a + PX)))) << 4);
    const uint16_t ay = (uint16_t)(absw(s16((uint32_t)(m.rw(a2 + PY) - m.rw(a + PY)))) << 4);
    const uint16_t d = octant(ax, ay);
    if (d < best) {
      best = d;
      p = a;
    }
  }

  int16_t dx = s16((uint32_t)(m.rw(a2 + PX) - m.rw(p + PX)));
  int16_t dy = s16((uint32_t)(m.rw(a2 + PY) - m.rw(p + PY)));
  uint8_t d;
  if (m.rb(a2 + TYPE) == STEELIE) {
    const uint16_t ax = (uint16_t)absw(dx) >> 4, ay = (uint16_t)absw(dy) >> 4;
    d = ay == 0 ? 0 : (ax > ay ? 1 : (ay > ax ? 3 : 2));
    if (dx > 0 && dy < 0) {
      d += 12;
    } else if (dx < 0 && dy < 0) {
      d += 8;
    } else if (dx < 0 && dy > 0) {
      d += 4;
    }
  } else {
    dx >>= 4;
    dy >>= 4;
    d = dy > 0 ? 4 : (dy < 0 ? 12 : (dx > 0 ? 0 : 8));
  }
  m.wb(a2 + DIR, d);
  turn(m, a2, TRIES_CHASE, SKIP_BACK);
}

// 0x1953e (disassembly): walk anim, slinky/acid by the v words (y sign first)
void walk_1953e(Mem &m, uint32_t a2) {
  const uint8_t t = m.rb(a2 + TYPE);
  if (t == STEELIE) {
    m.wl(a2 + ANIM, WALK_STEELIE);
    return;
  }
  if (t != SLINKY && t != ACID) { return; }

  const int16_t vx = m.sw(a2 + VX), vy = m.sw(a2 + VY);
  const uint8_t k = vy > 0 ? 0 : (vy < 0 ? 1 : (vx > 0 ? 2 : (vx < 0 ? 3 : 4)));
  m.wl(a2 + ANIM, (t == SLINKY ? WALK_SLINKY : WALK_ACID)[k]);
}

// 0x194ba: by kind: wander or chase, then the walk anim; collected: its anim
void dispatch_194ba(Mem &m, uint32_t a2) {
  const int8_t k = m.sb(a2 + KIND);
  const uint8_t t = m.rb(a2 + TYPE);
  if (k == WANDER || k == NEAR) {
    if (k == WANDER) {
      wander_1960e(m, a2);
    } else {
      chase_1973c(m, a2);
    }
    walk_1953e(m, a2);
  } else if (k == COLLECTED) {
    m.wl(a2 + ANIM, t == STEELIE ? GONE_STEELIE : (t == SLINKY ? GONE_SLINKY : GONE_OTHER));
  }
}

// 0x18ffa: all nine at random free spots of the field, 3 of each type; not while one lives
void spawn_18ffa(Mem &m) {
  for (uint8_t i = 0; i < NMINIS; i++) {
    if (m.rb(mini(i) + SLOT) == LIVE) { return; }
  }
  for (uint8_t i = 0; i < NMINIS; i++) {
    const uint32_t a = mini(i);
    m.wb(a + TYPE, i < 3 ? STEELIE : (i < 6 ? SLINKY : ACID));
    m.wb(a + SLOT, LIVE);
    m.wb(a + KIND, WANDER);
    do {
      m.wl(a + PX, (uint32_t)(marble_rng::rng(m, SPAWN_RNG) * 4 + FIELD_X) << 16);
      m.wl(a + PY, (uint32_t)(marble_rng::rng(m, SPAWN_RNG) * 4 + FIELD_Y) << 16);
    } while (blocked_1937c(m, a));
    m.wl(a + PZ, Z0);
    m.wl(a + VZ, 0);
    m.wl(a + VY, 0);
    m.wl(a + VX, 0);
    m.wb(a + STEPS, 0);
    dispatch_194ba(m, a);
    coords_199d6(m, a);
    marble_drawlist::insert(m, a, m.rb(a + TYPE));
  }
}

// 0x190ee: all nine out
void clear_190ee(Mem &m) {
  for (uint8_t i = 0; i < NMINIS; i++) {
    const uint32_t a = mini(i);
    m.wb(a + SLOT, 0);
    marble_drawlist::remove(m, a, m.rb(a + TYPE));
  }
}

// a live player on the field floor in the field zone
bool playerNear(const Mem &m) {
  bool near = false;
  for (uint16_t k = 0; k < m.rw(PLAYERS); k++) {
    const uint32_t a = marble_physics::PLAYER + PLAYER_SIZE * k;
    if (m.rb(a + P_SLOT) == LIVE && m.rw(a + PZ) == FIELD_FLOOR && m.rb(a + P_ZONE) == FIELD_ZONE) {
      near = true;
    }
  }
  return near;
}

// step by v
void stepV(Mem &m, uint32_t a2) {
  m.wl(a2 + PX, m.rl(a2 + PX) + m.rl(a2 + VX));
  m.wl(a2 + PY, m.rl(a2 + PY) + m.rl(a2 + VY));
}

// the animation cursor steps; true at the list end
bool animEnd(Mem &m, uint32_t a2) {
  m.wl(a2 + ANIM, m.rl(a2 + ANIM) + 4);
  return m.roml(m.rl(a2 + ANIM)) == NO_FRAME;
}

// 0x1912c per mini: ticker (NEAR every tick, else every 3rd); steelie steps by v (a new
// direction every 4th step; collected: anim), the others step at each anim list end
void tick(Mem &m, uint32_t a2, bool near) {
  static const uint8_t EVERY_NEAR = 1, EVERY = 3, STEELIE_STEPS = 4;
  m.wb(a2 + TICK, m.rb(a2 + TICK) + 1);
  if (m.rb(a2 + KIND) != COLLECTED) { m.wb(a2 + KIND, near ? NEAR : WANDER); }
  if ((m.rb(a2 + KIND) == NEAR ? EVERY_NEAR : EVERY) > m.sb(a2 + TICK)) {
    coords_199d6(m, a2);
    return;
  }

  m.wb(a2 + TICK, 0);
  const bool steelie = m.rb(a2 + TYPE) == STEELIE;
  if (steelie && m.rb(a2 + KIND) == COLLECTED) {
    if (animEnd(m, a2)) { dispatch_194ba(m, a2); }
  } else if (steelie) {
    stepV(m, a2);
    m.wb(a2 + STEPS, m.rb(a2 + STEPS) + 1);
    if (m.sb(a2 + STEPS) >= STEELIE_STEPS) {
      dispatch_194ba(m, a2);
      m.wb(a2 + STEPS, 0);
    }
  } else if (animEnd(m, a2)) {
    stepV(m, a2);
    dispatch_194ba(m, a2);
  }
  coords_199d6(m, a2);
}

}  // namespace

void init(Mem &m) {
  for (uint8_t i = 0; i < NMINIS; i++) { m.wb(mini(i) + IDX, i); }
}

// 0x144e4 mode 4 (silly race), mini part: entering 0x1d-0x38 spawns, leaving clears
void bands(Mem &m, int8_t a, int8_t b) {
  if (m.rw(marble_physics::RACE) != SILLY) { return; }
  if (!inBand(a) && inBand(b)) { spawn_18ffa(m); }
  if (inBand(a) && !inBand(b)) { clear_190ee(m); }
}

void update_1912c(Mem &m) {
  if (m.rw(marble_physics::RACE) != SILLY) { return; }
  const bool near = playerNear(m);
  for (uint8_t i = 0; i < NMINIS; i++) {
    if (m.rb(mini(i) + SLOT)) { tick(m, mini(i), near); }
  }
}

// 0x1924e (silly race, player a2 in the field zone; scratch 0x400690 = its new position):
// every live mini not collected within 4 px (steelie 5) on both axes is collected (sounds
// 0x5a-0x5c): race time +3 s, points
void contact(Mem &m, uint32_t a2) {
  if (m.rw(marble_physics::RACE) != SILLY || m.rb(a2 + P_ZONE) != FIELD_ZONE) { return; }

  for (uint8_t i = 0; i < NMINIS; i++) {
    const uint32_t a3 = mini(i);
    if (!m.rb(a3 + SLOT) || m.rb(a3 + KIND) == COLLECTED) { continue; }
    const int16_t r = m.rb(a3 + TYPE) == STEELIE ? HIT_PX_STEELIE : HIT_PX;
    if (r <= absw(s16((uint32_t)(m.sw(POS_X) - m.sw(a3 + PX)))) ||
        r <= absw(s16((uint32_t)(m.sw(POS_Y) - m.sw(a3 + PY))))) {
      continue;
    }

    m.wb(a3 + KIND, COLLECTED);
    m.wl(a3 + VY, 0);
    m.wl(a3 + VX, 0);
    dispatch_194ba(m, a3);
    if (m.rb(a3 + TYPE) == ACID) { m.wl(a3 + ANIM, HIT_ACID); }
    if (m.rb(a3 + TYPE) == STEELIE) { m.wl(a3 + ANIM, HIT_STEELIE); }
    m.ww(a2 + P_TIME, m.rw(a2 + P_TIME) + TIME_BONUS);
    marble_physics::score(m, a2, POINTS);
    m.wl(a2 + P_SCORE, m.rl(a2 + P_SCORE) + (uint32_t)POINTS_HIT);
    const uint8_t bit = m.rb(a2 + IDX) & 0x3f;
    if (bit < 8) { m.wb(SCORED, m.rb(SCORED) | (uint8_t)(1u << bit)); }
  }
}

}  // namespace marble_minis

namespace marble_enemies {

using marble_physics::Mem;

void miniInit(Mem &m) { marble_minis::init(m); }
void miniBands(Mem &m, int8_t a, int8_t b) { marble_minis::bands(m, a, b); }
void minis_1912c(Mem &m) { marble_minis::update_1912c(m); }
void contact_1924e(Mem &m, uint32_t a2) { marble_minis::contact(m, a2); }

}  // namespace marble_enemies

#endif  // ENABLE_MARBLE
