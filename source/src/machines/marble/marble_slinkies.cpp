#include "marble_slinkies.h"
#include "marble_enemies.h"
#include "marble_drawlist.h"
#include "marble_occlusion.h"
#include "marble_physics.h"
#include "marble_sound.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

// 1:1 port of tools/marble_slinky.py (function names as there; Marble Love state-sub-14c46.ts,
// sub-14966.ts, helper-15148.ts, state-dispatch-15460.ts, state-sub-15670.ts,
// nearest-neighbor.ts, sprite-coords-jsr-150d0.ts, script-slot-bbox-test-14e92.ts; the lunge
// 0x14a28 from the disassembly). Integer semantics follow the model.
//
//   state +0x1a:  WALK --contact--> PUSHED --still--> STOPPED --> CHASE --catch--> ATE --> BACK
//                   ^                                                \--out of reach--> LOST -/
//                   \------------------- next waypoint reached ---------------------------/
// A walking slinky that sees a rolling player 24-40 px away also starts the CHASE.

// everything in its own namespace: the host tests compile every module into one unit, where
// marble_enemies.cpp's steelie constants (same names) would hide these
namespace marble_slinkies {

using marble_physics::Mem;
using marble_physics::s8;
using marble_physics::s16;
using marble_enemies::NSLINKIES;
using marble_enemies::SLINKIES;
using marble_enemies::SLINKY_SIZE;

namespace {

// slinky fields (MARBLE.md "Slinkies in the main tick model")
enum : uint8_t {
  VX = 0x00, VY = 0x04, PX = 0x0c, PY = 0x10, PZ = 0x14,
  SLOT = 0x18, IDX = 0x19, STATE = 0x1a, ZONE = 0x1b,
  PUSH_VX = 0x1c, PUSH_VY = 0x20,  // marble velocity at the push
  TICK = 0x24, TICKS = 0x25,       // frame ticker, ticks per frame
  STEP = 0x26,                     // frame step: 1, 0xff backwards
  LUNGE = 0x27,                    // lunge tick (states 1/5/6)
  SCREEN = 0x28,                   // sx, sy words
  MASKS = 0x2c,                    // 5 x {code, x, y}
  NEXT = 0x4a, PATH = 0x4e,        // longs: next waypoint, waypoint list (4 byte records)
  BAND_LO = 0x52, BAND_HI = 0x54,
  PREY = 0x56,                     // word: index into OBJ_PTRS
  CURSOR = 0x58, ANIM = 0x5c,      // longs: frame cursor, animation list
};

enum State : uint8_t { WALK, CHASE, PUSHED, BACK, STOPPED, ATE, LOST };

// player fields
enum : uint8_t {
  P_SLOT = 0x18, P_NO = 0x19, P_STATE = 0x1a, P_ZONE = 0x1b, P_VISIBLE = 0x1c, P_AIR = 0x36,
  P_DAMAGE = 0x57, P_ANIM = 0x5a, P_ANIM_5F = 0x5f, P_ANIM_60 = 0x60, P_DIZZY_CNT = 0x56,
};

static const uint8_t LIVE = 1, MASK_N = 5, MASK_ENTRY = 6;
static const uint8_t TYPE_SLINKY = 4;            // draw list type
static const uint8_t SPAWN_ENTRY = 8, WAYPOINT = 4, END = 0xff;
static const uint8_t EATEN_TICKS = 0x32, LOST_SPARKLE = 0x65;
static const uint8_t AIRBORNE = 2;

// work RAM
static const uint32_t PLAYERS = 0x400396;
static const uint32_t OLD_X = 0x400684, OLD_Y = 0x400688;   // longs
static const uint32_t POS_X = 0x400690, POS_Y = 0x400692, POS_Z = 0x400694;  // words
static const uint16_t PLAYER_SIZE = 0xe2;

// program ROM
static const uint32_t SPAWN_TABLE = 0x2257a;  // per race: 8 byte entries, 0xff end
static const uint32_t OBJ_PTRS = 0x1eff6;     // P1, P2, steelie 0, steelie 1
static const uint32_t REAR = 0x2255a;         // s8 z lift (px) per lunge tick 6..0x25
static const uint32_t NO_FRAME = 0xffffffff;

// animation lists (0x15460)
static const uint32_t ANIM_IDLE = 0x20c18, ANIM_RIGHT = 0x20c48, ANIM_DOWN = 0x20c6c;
static const uint32_t ANIM_UP = 0x20c90, ANIM_LEFT = 0x20cb4, ANIM_CHASE = 0x20cd8;
static const uint32_t ANIM_PUSHED = 0x20d64, ANIM_LOST = 0x20d6c, ANIM_ATE = 0x20e28;
static const uint32_t ANIM_STOP_XP = 0x20dd8, ANIM_STOP_XN = 0x20e14;
static const uint32_t ANIM_STOP_YP = 0x20dec, ANIM_STOP_YN = 0x20e00;
static const uint32_t EAT_ROLL = 0x20faa, EAT_DIZZY = 0x20fb6;  // player +0x5a at the catch

static const int32_t CELL = 0x80000, HALF_CELL = 0x40000;   // 8 px cell, 16.16
static const uint32_t CELL_MASK = 0xfff80000;
static const int8_t AXIS_STEP = 8;              // walk velocity (px per tick) << 16
static const uint16_t REACH = 0x100;            // chase: out of reach on an axis
static const uint16_t CATCH = 0x70, PULL = 0xc0;  // chase: octant distance to the prey
static const uint16_t SPOT_MIN = 0x180, SPOT_MAX = 0x280;  // 0x15670: chase start distance
static const uint32_t PREY_SPEED = 0xc000;      // 0x15670: |vx| + |vy| of a rolling player
static const uint16_t WAYPOINT_FAR = 0x400;
static const int8_t LUNGE_FIRST = 6, LUNGE_END = 0x26;
static const int16_t BOX_XY = 3, BOX_Z = 0xe, SLINKY_Z = 0x10;
static const int8_t BOX_DEFAULT = -4, BOX_DEFAULT_SIZE = 8;
static const int32_t PULL_K = 0x400, HOME_K = 0x800;

inline int32_t s32(int64_t v) { return (int32_t)(uint32_t)v; }
inline int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

uint32_t slinky(uint8_t i) { return SLINKIES + SLINKY_SIZE * i; }

int16_t cell(uint32_t v) { return s16((uint32_t)((int32_t)v >> 19)); }

// max + 3/8 min of two words
uint16_t octant(uint16_t a, uint16_t b) {
  return (uint16_t)(a > b ? (b >> 3) * 3 + a : (a >> 3) * 3 + b);
}

// octant of |d| >> 12 (1/16 px deltas, 16.16 positions)
uint16_t octantOf(int64_t dx, int64_t dy) {
  return octant((uint16_t)(abs64(dx) >> 12), (uint16_t)(abs64(dy) >> 12));
}

// divs.w quotient; on overflow the 68000 leaves the numerator, its low word is used
int16_t divs(int64_t n, int64_t d) {
  const int64_t q = n / d;
  return (q >= -0x8000 && q <= 0x7fff) ? (int16_t)q : s16((uint32_t)n);
}

// waypoint record byte k (0: cell x, 1: cell y, 2: next record offset)
int8_t wp(const Mem &m, uint32_t p, uint8_t k) { return s8(m.romb(p + k)); }

uint32_t prey(const Mem &m, uint32_t a2) { return m.roml(OBJ_PTRS + 4 * m.sw(a2 + PREY)); }

void groundZ(Mem &m, uint32_t a2) {
  m.wl(a2 + PZ, (uint32_t)marble_physics::cellHeight(m, a2));
}

// 0x150d0: screen position +0x28 as slot objects, then the masks (0x264aa mode 2)
void coords_150d0(Mem &m, uint32_t a2) {
  const uint16_t x = m.rw(a2 + PX), y = m.rw(a2 + PY);
  m.ww(POS_X, x);
  m.ww(POS_Y, y);
  const uint16_t sx = (uint16_t)(y - x + 0x88);
  const uint16_t sy = (uint16_t)(m.rw(marble_physics::CAMERA) + m.rw(a2 + PZ) + 0x54 -
                                 (uint16_t)(((int32_t)s16(x) + s16(y)) >> 1));
  m.wl(a2 + SCREEN, (uint32_t)sx << 16 | sy);
  marble_occlusion::slinky(m, a2);
}

// 0x14c46 init of a free slinky from spawn entry e: first waypoint, walking, in the draw list
void init_14c46(Mem &m, uint32_t a2, uint32_t e) {
  const uint32_t path = m.roml(e + 2);
  m.wl(a2 + NEXT, path);
  m.wl(a2 + PATH, path);
  m.wb(a2 + ZONE, m.romb(e + 6));
  m.ww(a2 + BAND_LO, (uint16_t)(int16_t)s8(m.romb(e)));
  m.ww(a2 + BAND_HI, (uint16_t)(int16_t)s8(m.romb(e + 1)));
  m.wl(a2 + PX, (uint32_t)(wp(m, path, 0) * CELL + HALF_CELL));
  m.wl(a2 + PY, (uint32_t)(wp(m, path, 1) * CELL + HALF_CELL));
  groundZ(m, a2);

  m.wl(a2 + VY, 0);
  m.wl(a2 + VX, 0);
  m.wb(a2 + SLOT, LIVE);
  m.wb(a2 + STATE, WALK);
  m.wl(a2 + SCREEN, 0);
  for (uint8_t i = 0; i < MASK_N; i++) { m.ww(a2 + MASKS + MASK_ENTRY * i, 0); }
  m.wl(a2 + ANIM, ANIM_IDLE);
  m.wl(a2 + CURSOR, ANIM_IDLE);
  m.wb(a2 + STEP, 1);
  m.wb(a2 + TICK, 0);
  m.wb(a2 + TICKS, 2);
  coords_150d0(m, a2);
  marble_drawlist::insert(m, a2, TYPE_SLINKY);
}

// one axis step of 8 towards t (0 when there)
int8_t towards(int16_t c, int16_t t) { return c > t ? -AXIS_STEP : (c < t ? AXIS_STEP : 0); }

// 0x15460: animation list +0x5c by state, frame cursor to its start, ticker reset
void anim_15460(Mem &m, uint32_t a2) {
  switch (m.rb(a2 + STATE)) {
    case WALK:
    case BACK: {  // one axis towards the next waypoint, x first when not moving in x
      const int16_t cx = cell(m.rl(a2 + PX)), cy = cell(m.rl(a2 + PY));
      const uint32_t n = m.rl(a2 + NEXT);
      const int16_t tx = wp(m, n, 0), ty = wp(m, n, 1);
      int8_t dx = 0, dy = 0;
      if (m.sl(a2 + VX) == 0) {
        dx = towards(cx, tx);
        if (!dx) { dy = towards(cy, ty); }
      } else {
        dy = towards(cy, ty);
        if (!dy) { dx = towards(cx, tx); }
      }
      const uint32_t anim = dy > 0 ? ANIM_DOWN : dy < 0 ? ANIM_UP
                          : dx > 0 ? ANIM_RIGHT : dx < 0 ? ANIM_LEFT : ANIM_IDLE;
      m.wl(a2 + ANIM, anim);
      m.wl(a2 + VX, (uint32_t)((int32_t)dx * 0x10000));
      m.wl(a2 + VY, (uint32_t)((int32_t)dy * 0x10000));
      m.wb(a2 + STEP, 1);
      break;
    }
    case CHASE:
      m.wl(a2 + ANIM, ANIM_CHASE);
      m.wb(a2 + LUNGE, 0);
      m.wb(a2 + STEP, 1);
      break;
    case PUSHED: {  // the push animation plays out (backwards for its second half)
      const uint32_t cur = m.rl(a2 + ANIM), prev = m.rl(a2 + CURSOR);
      if (cur == ANIM_PUSHED) { break; }
      if (cur == prev) {
        m.wl(a2 + ANIM, ANIM_PUSHED);
        m.wb(a2 + STEP, 1);
      } else {
        m.wb(a2 + STEP, 4 > s16((uint32_t)(s32((int64_t)prev - cur) >> 2)) ? 0xff : 1);
      }
      break;
    }
    case STOPPED: {  // facing the push direction
      const int32_t vx = m.sl(a2 + PUSH_VX), vy = m.sl(a2 + PUSH_VY);
      const uint32_t anim = abs64(vx) > abs64(vy) ? (vx > 0 ? ANIM_STOP_XP : ANIM_STOP_XN)
                                                   : (vy > 0 ? ANIM_STOP_YP : ANIM_STOP_YN);
      m.wl(a2 + ANIM, anim);
      m.wb(a2 + STEP, 1);
      break;
    }
    case ATE: m.wl(a2 + ANIM, ANIM_ATE); break;
    case LOST: m.wl(a2 + ANIM, ANIM_LOST); break;
    default: break;
  }
  m.wl(a2 + CURSOR, m.rl(a2 + ANIM));
  m.wb(a2 + TICK, 0);
  const uint8_t st = m.rb(a2 + STATE);
  m.wb(a2 + TICKS, (st == WALK || st == STOPPED) ? 2 : 1);
}

// another chasing slinky has this prey key
bool taken(const Mem &m, uint32_t a2, uint16_t key) {
  for (uint8_t i = 0; i < NSLINKIES; i++) {
    const uint32_t s = slinky(i);
    if (s != a2 && m.rb(s + SLOT) == LIVE && m.rb(s + STATE) == CHASE && m.rw(s + PREY) == key) {
      return true;
    }
  }
  return false;
}

// 0x15670: a walking slinky picks the last rolling player in its zone (moving, on the
// ground, not taken by another chasing slinky) as prey; 24-40 px away it starts the chase
void prey_15670(Mem &m, uint32_t a2) {
  const uint16_t n = m.rw(PLAYERS);
  uint8_t rem = (uint8_t)n;
  uint32_t a1 = 0;
  for (uint16_t i = 0; i < n; i++) {
    const uint32_t o = marble_physics::PLAYER + PLAYER_SIZE * i;
    if (m.rb(o + P_SLOT) != LIVE || m.rb(o + P_ZONE) != m.rb(a2 + ZONE)) { continue; }
    if (abs64(m.sl(o + VX)) + abs64(m.sl(o + VY)) <= PREY_SPEED) { continue; }
    const uint8_t st = m.rb(o + P_STATE);
    if (m.rb(o + P_AIR) == AIRBORNE ||
        (st != marble_physics::ROLL && st != marble_physics::DIZZY && st != marble_physics::EATEN)) {
      continue;
    }
    if (taken(m, a2, (uint16_t)(int16_t)s8(m.rb(o + P_NO)))) { continue; }
    rem--;
    a1 = o;
  }
  if ((uint16_t)(int16_t)s8(rem) == n) { return; }
  if (n == 2 && rem == 0) { marble_physics::fail("0x15fe6 two players", a2); }

  const uint16_t d = octantOf(s32((int64_t)m.sl(a1 + PX) - m.sl(a2 + PX)),
                              s32((int64_t)m.sl(a1 + PY) - m.sl(a2 + PY)));
  m.ww(a2 + PREY, (uint16_t)(int16_t)s8(m.rb(a1 + P_NO)));
  if (d <= SPOT_MIN || d >= SPOT_MAX) { return; }
  m.wb(a2 + STATE, CHASE);
  anim_15460(m, a2);
}

// 0x14dec: next waypoint = nearest record of the list (octant of the cell deltas x16)
void nearest_14dec(Mem &m, uint32_t a2) {
  const int16_t cx = cell(m.rl(a2 + PX)), cy = cell(m.rl(a2 + PY));
  uint32_t bp = NO_FRAME;
  uint16_t best = WAYPOINT_FAR;
  for (uint32_t p = m.rl(a2 + PATH); m.romb(p) != END && m.romb(p + 1) != END; p += WAYPOINT) {
    const uint16_t dx = (uint16_t)(abs64(s16(cx - wp(m, p, 0))) << 4);
    const uint16_t dy = (uint16_t)(abs64(s16(cy - wp(m, p, 1))) << 4);
    const uint16_t d = octant(dx, dy);
    if (d < best) {
      best = d;
      bp = p;
    }
  }
  m.wl(a2 + NEXT, bp);
}

// next waypoint = the record after waypoint n
void advance(Mem &m, uint32_t a2, uint32_t n) {
  m.wl(a2 + NEXT, m.rl(a2 + PATH) + wp(m, n, 2) * WAYPOINT);
}

// 0x15196, states 0/3: back on the list's first waypoint: stop, head for the next; on the next
// waypoint: advance; then re-aim and, walking, look for prey
void walk_15196(Mem &m, uint32_t a2) {
  const int16_t cx = cell(m.rl(a2 + PX)), cy = cell(m.rl(a2 + PY));
  const uint32_t first = m.rl(a2 + PATH);
  if (cx == wp(m, first, 0) && cy == wp(m, first, 1) && m.rl(a2 + ANIM) != ANIM_IDLE) {
    if (m.rb(a2 + STATE) == BACK) { m.wb(a2 + STATE, WALK); }
    m.wl(a2 + ANIM, ANIM_IDLE);
    m.wl(a2 + VY, 0);
    m.wl(a2 + VX, 0);
    m.wb(a2 + STEP, 1);
    m.wl(a2 + CURSOR, ANIM_IDLE);
    m.wb(a2 + TICK, 0);
    m.wb(a2 + TICKS, 2);
    advance(m, a2, m.rl(a2 + NEXT));
  } else {
    const uint32_t n = m.rl(a2 + NEXT);
    if (cx == wp(m, n, 0) && cy == wp(m, n, 1)) {
      if (m.rb(a2 + STATE) == BACK) { m.wb(a2 + STATE, WALK); }
      advance(m, a2, n);
    }
    anim_15460(m, a2);
  }
  if (m.rb(a2 + STATE) == WALK) { prey_15670(m, a2); }
}

// 0x15316, state 1: prey within 0x70 (octant): caught (prey hidden at the slinky, state 7,
// slinky ATE); within 0xc0 the prey is pulled in; out of reach or not caught: LOST
void chase_15316(Mem &m, uint32_t a2) {
  const uint32_t o = prey(m, a2);
  const int32_t dx = s32((int64_t)m.sl(o + PX) - m.sl(a2 + PX));
  const int32_t dy = s32((int64_t)m.sl(o + PY) - m.sl(a2 + PY));
  const uint16_t ax = (uint16_t)(abs64(dx) >> 12), ay = (uint16_t)(abs64(dy) >> 12);
  if (ax > REACH || ay > REACH) {
    m.wb(a2 + STATE, LOST);
    anim_15460(m, a2);
    return;
  }

  const uint16_t d = octant(ax, ay);
  if (d < CATCH) {
    marble_sound::send(m, marble_sound::SND_CAUGHT);
    m.wl(o + PX, m.rl(a2 + PX));
    m.wl(o + PY, m.rl(a2 + PY));
    m.wb(o + P_STATE, marble_physics::HIDDEN);
    m.wb(a2 + STATE, ATE);
    anim_15460(m, a2);
    return;
  }
  if (d < PULL && d) {
    m.wl(o + VX, (uint32_t)((PULL_K * divs(dx, d)) >> 4));
    m.wl(o + VY, (uint32_t)((PULL_K * divs(dy, d)) >> 4));
  }
  m.wb(a2 + STATE, LOST);
  anim_15460(m, a2);
}

// 0x1527c, state 2: carried by the marble's push with friction; stops when still; a step
// that changes the ground height is undone (up: CHASE, down: STOPPED)
void pushed_1527c(Mem &m, uint32_t a2) {
  marble_physics::drag(m, a2);
  if (m.sl(a2 + VX) == 0 && m.sl(a2 + VY) == 0) {
    m.wb(a2 + STATE, STOPPED);
    anim_15460(m, a2);
    return;
  }

  const uint32_t x = m.rl(a2 + PX), y = m.rl(a2 + PY), z = m.rl(a2 + PZ);
  m.wl(a2 + PX, x + m.rl(a2 + VX));
  m.wl(a2 + PY, y + m.rl(a2 + VY));
  groundZ(m, a2);
  const uint32_t nz = m.rl(a2 + PZ);
  if (nz != z) {
    m.wb(a2 + STATE, (int32_t)z > (int32_t)nz ? STOPPED : CHASE);
    m.wl(a2 + PX, x);
    m.wl(a2 + PY, y);
    m.wl(a2 + PZ, z);
  }
  anim_15460(m, a2);
}

// 0x153ea, states 5/6: snapped to the cell centre; ATE sends the prey lost (sound 0x15884);
// then BACK to the nearest waypoint
void done_153ea(Mem &m, uint32_t a2) {
  m.wl(a2 + PX, (m.rl(a2 + PX) & CELL_MASK) + HALF_CELL);
  m.wl(a2 + PY, (m.rl(a2 + PY) & CELL_MASK) + HALF_CELL);
  if (m.rb(a2 + STATE) == ATE) {
    const uint32_t o = prey(m, a2);
    m.wb(o + P_DAMAGE, LOST_SPARKLE);
    marble_physics::enterState(m, o, marble_physics::LOST);
  }
  m.wb(a2 + STATE, BACK);
  nearest_14dec(m, a2);
  anim_15460(m, a2);
}

// 0x15148: state machine
void state_15148(Mem &m, uint32_t a2) {
  switch (m.rb(a2 + STATE)) {
    case WALK:
    case BACK: walk_15196(m, a2); break;
    case CHASE: chase_15316(m, a2); break;
    case PUSHED: pushed_1527c(m, a2); break;
    case STOPPED:
      m.wb(a2 + STATE, CHASE);
      anim_15460(m, a2);
      break;
    case ATE:
    case LOST: done_153ea(m, a2); break;
    default: break;
  }
}

// v += ((0x800 * (d / octant) >> 4) - v) >> 5 towards the prey, v = d >> 4 first
void home(Mem &m, uint32_t a2, uint32_t o) {
  const int32_t dx = s32((int64_t)m.sl(o + PX) - m.sl(a2 + PX));
  const int32_t dy = s32((int64_t)m.sl(o + PY) - m.sl(a2 + PY));
  m.wl(a2 + VX, (uint32_t)(dx >> 4));
  m.wl(a2 + VY, (uint32_t)(dy >> 4));
  const uint16_t d = octantOf(dx, dy);
  const int16_t q[2] = {d ? divs(dx, d) : (int16_t)0, d ? divs(dy, d) : (int16_t)0};
  for (uint8_t k = 0; k < 2; k++) {
    const uint32_t a = a2 + (k ? VY : VX);
    const int32_t v = m.sl(a), aim = (HOME_K * q[k]) >> 4;
    m.wl(a, (uint32_t)(v + (s32((int64_t)aim - v) >> 5)));
  }
}

// 0x14a28 (states 1/5/6, from the disassembly): lunge ticks 6..0x25 rear the slinky up (z =
// ground + table) and, while its prey stands on the same ground in its zone, home in on it;
// a step onto other ground is undone
void lunge_14a28(Mem &m, uint32_t a2) {
  const int8_t c = m.sb(a2 + LUNGE);
  if (c >= LUNGE_FIRST && c < LUNGE_END) {
    const int32_t d5 = marble_physics::cellHeight(m, a2);
    m.wl(a2 + PZ, (uint32_t)((int32_t)s8(m.romb(REAR + c - LUNGE_FIRST)) * 0x10000 + d5));
    const uint32_t o = prey(m, a2);
    const uint8_t st = m.rb(o + P_STATE);
    if (m.rb(o + P_SLOT) == LIVE && m.rb(o + P_ZONE) == m.rb(a2 + ZONE) && !m.rb(o + P_AIR) &&
        m.sl(o + PZ) == d5 &&
        (st == marble_physics::ROLL || st == marble_physics::DIZZY ||
         st == marble_physics::EATEN || st == marble_physics::HIDDEN)) {
      home(m, a2, o);
    } else {
      m.wl(a2 + VX, 0);
      m.wl(a2 + VY, 0);
    }
    m.wl(OLD_X, m.rl(a2 + PX));
    m.wl(OLD_Y, m.rl(a2 + PY));
    m.wl(a2 + PX, m.rl(a2 + PX) + m.rl(a2 + VX));
    m.wl(a2 + PY, m.rl(a2 + PY) + m.rl(a2 + VY));
    if (marble_physics::cellHeight(m, a2) != d5) {
      m.wl(a2 + PX, m.rl(OLD_X));
      m.wl(a2 + PY, m.rl(OLD_Y));
    }
  }
  m.wb(a2 + LUNGE, m.rb(a2 + LUNGE) + 1);
}

// 0x14966: frame ticker; at the end of a frame the cursor steps (list end or back at its start:
// restart, a walking slinky moves one step) and the state machine runs; a pushed slinky runs
// it every tick. Then the lunge (states 1/5/6), screen position and masks.
void tick_14966(Mem &m, uint32_t a2) {
  if (!m.rb(a2 + SLOT)) { return; }

  m.wb(a2 + TICK, m.rb(a2 + TICK) + 1);
  bool run = m.rb(a2 + STATE) == PUSHED;
  if (m.sb(a2 + TICKS) <= m.sb(a2 + TICK)) {
    m.wb(a2 + TICK, 0);
    const uint32_t p = m.rl(a2 + CURSOR) + (uint32_t)(m.sb(a2 + STEP) * 4);
    m.wl(a2 + CURSOR, p);
    if (m.roml(p) == NO_FRAME || p == m.rl(a2 + ANIM)) {
      m.wl(a2 + CURSOR, m.rl(a2 + ANIM));
      const uint8_t st = m.rb(a2 + STATE);
      if (m.sb(a2 + STEP) > 0 && (st == WALK || st == BACK)) {
        m.wl(a2 + PX, m.rl(a2 + PX) + m.rl(a2 + VX));
        m.wl(a2 + PY, m.rl(a2 + PY) + m.rl(a2 + VY));
      }
      run = true;
    }
  }
  if (run) { state_15148(m, a2); }

  const uint8_t st = m.rb(a2 + STATE);
  if (st == CHASE || st == ATE || st == LOST) { lunge_14a28(m, a2); }
  coords_150d0(m, a2);
}

// contact box offsets of a slinky's current frame (header bytes 4-7)
void box(const Mem &m, uint32_t a2, int16_t b[4]) {
  const uint32_t f = m.roml(m.rl(a2 + CURSOR));
  for (uint8_t k = 0; k < 4; k++) {
    const int8_t d = k < 2 ? BOX_DEFAULT : BOX_DEFAULT_SIZE;
    b[k] = f == NO_FRAME ? d : s8(m.romb(f + 4 + k));
  }
}

bool overlap(int16_t a0, int16_t a1, int16_t b0, int16_t b1) {
  return (b0 <= a0 && a0 <= b1) || (b0 <= a1 && a1 <= b1);
}

// 0x14e92 hit: a walking slinky is pushed with the marble's velocity; unless it is already
// chasing this player, the marble is held at its old position and wobbles (EATEN, 50 ticks)
void hit(Mem &m, uint32_t a2, uint32_t s) {
  if (m.rb(s + STATE) == WALK || m.rb(s + STATE) == BACK) {
    m.wb(s + STATE, PUSHED);
    anim_15460(m, s);
    m.wl(s + VX, m.rl(a2 + VX));
    m.wl(s + PUSH_VX, m.rl(a2 + VX));
    m.wl(s + VY, m.rl(a2 + VY));
    m.wl(s + PUSH_VY, m.rl(a2 + VY));
  }
  const uint8_t st = m.rb(s + STATE);
  const uint16_t key = (uint16_t)(int16_t)s8(m.rb(a2 + P_NO));
  if ((st == CHASE || st == LOST) && key == m.rw(s + PREY)) { return; }
  if (m.rb(a2 + P_STATE) != marble_physics::EATEN && st != ATE) { m.ww(s + PREY, key); }

  m.wl(a2 + PX, m.rl(OLD_X));
  m.wl(a2 + PY, m.rl(OLD_Y));
  m.wl(a2 + VY, 0);
  m.wl(a2 + VX, 0);
  const uint8_t pst = m.rb(a2 + P_STATE);
  if (pst != marble_physics::EATEN) {
    marble_sound::send(m, marble_sound::SND_DIZZY);
    m.wb(a2 + P_ANIM_5F, 0);
    m.wb(a2 + P_ANIM_60, 2);
    m.wl(a2 + P_ANIM, pst == marble_physics::DIZZY ? EAT_DIZZY : EAT_ROLL);
  }
  if (pst != marble_physics::EATEN && pst != marble_physics::HIDDEN) {
    m.wb(a2 + P_STATE, marble_physics::EATEN);
    m.wb(a2 + P_DIZZY_CNT, EATEN_TICKS);
  }
}

}  // namespace

void init(Mem &m) {
  for (uint8_t i = 0; i < NSLINKIES; i++) { m.wb(slinky(i) + IDX, i); }
}

// 0x14c46: spawn a slinky for every entry of 0x2257a[race] whose band lo or hi is entered from
// outside [lo, hi] and whose waypoint list no live slinky has (free slot: highest free index,
// none free ends the scan); then remove slinkies whose band range is left.
void bands(Mem &m, int8_t a, int8_t b) {
  for (uint32_t e = m.roml(SPAWN_TABLE + 4 * m.rw(marble_physics::RACE)); m.romb(e) != END;
       e += SPAWN_ENTRY) {
    uint32_t free = 0;
    bool used = false;
    for (uint8_t i = 0; i < NSLINKIES; i++) {
      const uint32_t s = slinky(i);
      if (!m.rb(s + SLOT)) {
        free = s;
      } else if (m.rl(s + PATH) == m.roml(e + 2)) {
        used = true;
      }
    }
    if (!free) { break; }

    const int8_t lo = s8(m.romb(e)), hi = s8(m.romb(e + 1));
    if (!used && (b == lo || b == hi) && !(lo <= a && a <= hi)) { init_14c46(m, free, e); }
  }

  for (uint8_t i = 0; i < NSLINKIES; i++) {
    const uint32_t s = slinky(i);
    if (!m.rb(s + SLOT)) { continue; }
    const int16_t lo = m.sw(s + BAND_LO), hi = m.sw(s + BAND_HI);
    if ((a == lo && b < lo) || (a == hi && b > hi)) {
      m.wb(s + SLOT, 0);
      marble_drawlist::remove(m, s, TYPE_SLINKY);
    }
  }
}

void update_1493c(Mem &m) {
  for (uint8_t i = 0; i < NSLINKIES; i++) { tick_14966(m, slinky(i)); }
}

// 0x14e92 (races 1/2/5): player a2 (scratch 0x400690 = its new position, box x/y +-3, z
// +0..+0xe) against the slinky frame boxes (z +0..+0x10); the first hit counts
void contact(Mem &m, uint32_t a2) {
  const uint16_t race = m.rw(marble_physics::RACE);
  if (race != 1 && race != 2 && race != 5) { return; }

  const int16_t wx = m.sw(POS_X), wy = m.sw(POS_Y), wz = m.sw(POS_Z);
  for (uint8_t i = 0; i < NSLINKIES; i++) {
    const uint32_t s = slinky(i);
    if (!m.rb(s + SLOT)) { continue; }
    int16_t b[4];
    box(m, s, b);
    const int16_t x0 = s16(m.rw(s + PX) + b[0]), y0 = s16(m.rw(s + PY) + b[1]);
    const int16_t z0 = m.sw(s + PZ);
    if (!overlap(wz, s16(wz + BOX_Z), z0, s16(z0 + SLINKY_Z))) { continue; }
    if (!overlap(s16(wx - BOX_XY), s16(wx + BOX_XY), x0, s16(x0 + b[2]))) { continue; }
    if (!overlap(s16(wy - BOX_XY), s16(wy + BOX_XY), y0, s16(y0 + b[3]))) { continue; }
    hit(m, a2, s);
    return;
  }
}

}  // namespace marble_slinkies

namespace marble_enemies {

using marble_physics::Mem;

void slinkyInit(Mem &m) { marble_slinkies::init(m); }
void slinkyBands(Mem &m, int8_t a, int8_t b) { marble_slinkies::bands(m, a, b); }
void slinkies_1493c(Mem &m) { marble_slinkies::update_1493c(m); }
void contact_14e92(Mem &m, uint32_t a2) { marble_slinkies::contact(m, a2); }

}  // namespace marble_enemies

#endif  // GAME_MARBLE
