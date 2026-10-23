#include "marble_enemies.h"
#include "marble_drawlist.h"
#include "marble_occlusion.h"
#include "marble_physics.h"
#include "marble_slinkies.h"
#include "marble_acid.h"
#include "marble_birds.h"
#include "marble_minis.h"
#include "marble_pistons.h"
#include "marble_sound.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include "marble_rom.h"

// 1:1 port of dev/marble/marble_enemy.py (function names as there; Marble Love sub-158f6.ts,
// helper-182ba.ts, helper-1bc88.ts, scroll-sub-15a12.ts, state-sub-15bd0.ts). Integer
// semantics follow the model: int64_t where its Python values exceed 32 bits.

namespace marble_enemies {

using marble_physics::Mem;
using marble_physics::pathByte;
using marble_physics::be32;

namespace {

// object fields (player layout, MARBLE.md "Enemies")
enum : uint8_t {
  VX = 0x00, VY = 0x04, VZ = 0x08, PX = 0x0c, PY = 0x10, PZ = 0x14,
  SLOT = 0x18,                 // 1 live, 2 shattering
  IDX = 0x19, MODE = 0x1a, ZONE = 0x1b,
  SCREEN = 0x1e, SCREEN_OLD = 0x22,
  AIR = 0x36,                  // 0 ground, 2 airborne
  MASKS = 0x38,                // 5 x {code, x, y}
  STUN_CNT = 0x56, DAMAGE = 0x57, SURFACE = 0x58, SHARDS_ON = 0x67,
  SPEED = 0x68,                // long, 16.16
  ANGER = 0x6c,                // word: 0xffff never hit, else ticks left
  NODE = 0x6e, NODES = 0x72,   // longs: current node, node list (6 byte nodes)
  BAND_LO = 0x76, BAND_HI = 0x78,
  TARGET = 0x7a,               // word: index into OBJ_PTRS
};

// steelie modes +0x1a
enum Mode : uint8_t { PATROL = 0x20, CHASE = 0x21, HUNT = 0x22, RETURN = 0x23, STUN = 0x24 };

static const uint8_t LIVE = 1, SHATTERING = 2;
static const uint8_t TYPE_STEELIE = 2;            // draw list type
static const uint8_t MASK_N = 5, MASK_ENTRY = 6;
static const uint8_t SURFACE_TUBE = 0xa;
static const uint8_t PLAYER_LOST = 4, PLAYER_CARRIED = 0xb;
static const uint8_t ZONE_A = 7, ZONE_B = 9;      // chase without anger there

// work RAM
static const uint32_t RACE = 0x400394, PLAYERS = 0x400396;
static const uint32_t OLD_X = 0x400684, OLD_Y = 0x400688, OLD_Z = 0x40068c;  // longs
static const uint32_t POS_X = 0x400690, POS_Y = 0x400692, POS_Z = 0x400694;  // words
static const uint16_t PLAYER_SIZE = 0xe2;

// program ROM
static const uint32_t SPAWN_HI16 = 0x2276e;   // beginner start entry: hi - 16
static const uint8_t NODE_SIZE = 6, LINKS = 4;
static const uint8_t END = 0xff;

static const uint16_t ANGRY_TICKS = 0x96, NEVER_HIT = 0xffff;
static const uint8_t STUN_HIT = 0x14;             // +0x56 added by a collision
static const uint8_t STUN_BREAK = 0x50, FALL_BREAK = 0x1f;
static const uint16_t CHASE_DIST = 0x280, NEAR_DIST = 0x40, NODE_FAR = 0x400;
static const uint16_t PAIR_DIST = 0x70;
static const int16_t PAIR_XY = 7, PAIR_Z = 14;
static const uint32_t SPEED_NEAR = 0x10000;
static const int32_t CELL = 0x80000, HALF_CELL = 0x40000;  // 8 px cell, 16.16
static const int32_t FALL_VZ = 0x6000, FALL_VZ_MAX = -0x50000;
static const uint8_t SCORE_STEELIE = 3;           // 0x285b0 points entry

inline int8_t s8(uint32_t v) { return (int8_t)v; }
inline int16_t s16(uint32_t v) { return (int16_t)v; }
inline int32_t s32(int64_t v) { return (int32_t)(uint32_t)v; }
inline int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

uint32_t steelie(uint8_t i) { return STEELIES + STEELIE_SIZE * i; }

uint32_t speedOf(uint8_t mode) {
  switch (mode) {
    case CHASE: return 0xc0000;
    case HUNT: return 0x80000;
    case PATROL: case RETURN: return 0x70000;
    default: return 0;
  }
}

// max + 3/8 min of |dx| >> 12, |dy| >> 12 (word)
uint16_t octant(int64_t dx, int64_t dy) {
  const uint16_t ax = (uint16_t)(abs64(dx) >> 12), ay = (uint16_t)(abs64(dy) >> 12);
  return (uint16_t)(ax > ay ? (ay >> 3) * 3 + ax : (ax >> 3) * 3 + ay);
}

// node byte k (0: cell x, 1: cell y, 2-5: links) of node n
int8_t node(const Mem &, uint32_t n, uint8_t k) { return s8(pathByte(n, k)); }

// 0x15d10: +0x6e = node of list +0x72 nearest to the steelie cell
void nearest_15d10(Mem &m, uint32_t a2) {
  const int16_t rx = s16((uint32_t)(m.sl(a2 + PX) >> 19)), ry = s16((uint32_t)(m.sl(a2 + PY) >> 19));
  uint32_t p = m.rl(a2 + NODES), bp = m.rl(a2 + NODE);
  uint16_t best = NODE_FAR;
  for (; pathByte(p, 0) != END && pathByte(p, 1) != END; p += NODE_SIZE) {
    const uint16_t dx = (uint16_t)(abs64(s16(rx - node(m, p, 0))) << 4);
    const uint16_t dy = (uint16_t)(abs64(s16(ry - node(m, p, 1))) << 4);
    const uint16_t d = (uint16_t)(dx <= dy ? (dx >> 3) * 3 + dy : (dy >> 3) * 3 + dx);
    if (d < best) {
      best = d;
      bp = p;
    }
  }
  m.wl(a2 + NODE, bp);
}

// 0x160d4: return to the nearest node
void enter23_160d4(Mem &m, uint32_t a2) {
  m.wb(a2 + MODE, RETURN);
  nearest_15d10(m, a2);
  m.wl(a2 + SPEED, speedOf(RETURN));
}

// 0x160ae: +0x6e = node of link k of the current node
void advance_160ae(Mem &m, uint32_t a2, uint8_t k) {
  m.wl(a2 + NODE, m.rl(a2 + NODES) + node(m, m.rl(a2 + NODE), 2 + k) * NODE_SIZE);
}

// 0x15c46: link (0-3) whose node is nearest to the target object
uint8_t best_15c46(const Mem &m, uint32_t a2) {
  const uint32_t t = marble_obj_targets[(uint16_t)m.sw(a2 + TARGET)];
  const uint32_t cur = m.rl(a2 + NODE), base = m.rl(a2 + NODES);
  uint8_t bi = 0;
  uint32_t bd = 0x7fffffff;
  for (uint8_t k = 0; k < LINKS; k++) {
    const uint32_t n = base + node(m, cur, 2 + k) * NODE_SIZE;
    const uint16_t d = octant((int64_t)node(m, n, 0) * CELL - m.sl(t + PX),
                              (int64_t)node(m, n, 1) * CELL - m.sl(t + PY));
    if (d < bd) {
      bd = d;
      bi = k;
    }
  }
  return bi;
}

// a player that can be chased: in play, same zone, on the ground or teetering, rolling
bool prey(const Mem &m, uint32_t o, uint32_t a2) {
  if (m.rb(o + SLOT) != LIVE || m.rb(o + ZONE) != m.rb(a2 + ZONE) || m.rb(o + AIR) == 2) {
    return false;
  }
  const uint8_t st = m.rb(o + MODE);
  return st == marble_physics::ROLL || st == marble_physics::DIZZY || st == marble_physics::EATEN;
}

// 0x15e24: pick the mode: chase (same zone, near, angry or zone 7/9, on the ground), hunt
// (angry), else patrol; a mode change sets the speed and the next node
void choose_15e24(Mem &m, uint32_t a2, bool flag) {
  const uint8_t old = m.rb(a2 + MODE);
  const uint16_t count = m.rw(PLAYERS);
  uint8_t rem = (uint8_t)count;
  uint32_t cand = 0;
  for (uint16_t i = 0; i < count; i++) {
    const uint32_t o = marble_physics::PLAYER + PLAYER_SIZE * i;
    if (!prey(m, o, a2)) { continue; }
    rem--;
    cand = o;
  }

  if ((uint16_t)(int16_t)s8(rem) != count) {
    if (count == 2 && rem == 0) { marble_physics::fail("0x15fe6 two players", a2); }
    m.ww(a2 + TARGET, (uint16_t)(int16_t)s8(m.rb(cand + IDX)));
    const uint16_t d = octant((int64_t)m.sl(cand + PX) - m.sl(a2 + PX),
                              (int64_t)m.sl(cand + PY) - m.sl(a2 + PY));
    const bool angry = m.rw(a2 + ANGER) != 0;
    const uint8_t zone = m.rb(a2 + ZONE);
    if (d < CHASE_DIST && (angry || zone == ZONE_A || zone == ZONE_B) && m.rb(a2 + AIR) == 0) {
      m.wb(a2 + MODE, CHASE);
    } else if (angry) {
      m.wb(a2 + MODE, HUNT);
    } else if (m.rb(a2 + MODE) != RETURN) {
      m.wb(a2 + MODE, PATROL);
    }
  } else if (m.rb(a2 + MODE) != RETURN) {
    m.wb(a2 + MODE, PATROL);
  }

  const uint8_t mode = m.rb(a2 + MODE);
  if (old != mode) {
    if (old == CHASE) {
      enter23_160d4(m, a2);
    } else {
      flag = true;
    }
  }
  if (!flag) { return; }

  m.wl(a2 + SPEED, speedOf(m.rb(a2 + MODE)));
  if (m.rb(a2 + MODE) == PATROL) {
    advance_160ae(m, a2, 0);
  } else if (m.rb(a2 + MODE) == HUNT) {
    advance_160ae(m, a2, best_15c46(m, a2));
  }
}

// 0x15db6: node reached (cell == node): return -> patrol; then nearest node or mode choice
void node_15db6(Mem &m, uint32_t a2) {
  const uint32_t n = m.rl(a2 + NODE);
  const bool hit = node(m, n, 0) == m.sl(a2 + PX) >> 19 && node(m, n, 1) == m.sl(a2 + PY) >> 19;
  if (hit && m.rb(a2 + MODE) == RETURN) { m.wb(a2 + MODE, PATROL); }
  if (m.rb(a2 + MODE) == RETURN) {
    nearest_15d10(m, a2);
    return;
  }
  choose_15e24(m, a2, hit);
}

// 0x182ba: steer the velocity toward the target (player in chase, else the node centre)
void ai_182ba(Mem &m, uint32_t a2) {
  node_15db6(m, a2);
  if (m.rb(a2 + AIR) != 2) {
    const uint32_t a0 = marble_obj_targets[(uint16_t)m.sw(a2 + TARGET)];
    int64_t d2, d3;
    const int8_t st = s8(m.rb(a0 + MODE));
    if (m.rb(a2 + MODE) == CHASE && m.rb(a0 + SLOT) == LIVE &&
        m.rb(a0 + ZONE) == m.rb(a2 + ZONE) && (st == 0 || st == 1 || st == 5)) {
      d2 = s32((int64_t)m.sl(a0 + PX) - m.sl(a2 + PX));
      d3 = s32((int64_t)m.sl(a0 + PY) - m.sl(a2 + PY));
    } else {
      const uint32_t a1 = m.rl(a2 + NODE);
      d2 = s32((int64_t)node(m, a1, 0) * CELL - m.sl(a2 + PX) + HALF_CELL);
      d3 = s32((int64_t)node(m, a1, 1) * CELL - m.sl(a2 + PY) + HALF_CELL);
    }

    const uint16_t d1 = octant(d2, d3);
    if (d1) {
      d2 = s16((uint32_t)(d2 / s16(d1)));
      d3 = s16((uint32_t)(d3 / s16(d1)));
    } else {
      d2 = d3 = 0;
    }
    if (d1 < NEAR_DIST) { m.wl(a2 + SPEED, SPEED_NEAR); }

    // v += ((k * dir >> 4) - v) >> 5, damped while stunned
    const int16_t k = s16((uint32_t)(m.sl(a2 + SPEED) >> 8));
    d2 = (s32(k * d2 >> 4) - (int64_t)m.sl(a2 + VX)) >> 5;
    d3 = (s32(k * d3 >> 4) - (int64_t)m.sl(a2 + VY)) >> 5;
    if (m.rb(a2 + MODE) == STUN) {
      int16_t c = s16(0x1f - m.sb(a2 + STUN_CNT));
      if (c < 0) { c = 0; }
      d2 = s32((d2 >> 8) * c * 8);
      d3 = s32((d3 >> 8) * c * 8);
    }
    m.wl(a2 + VX, (uint32_t)(m.sl(a2 + VX) + d2));
    m.wl(a2 + VY, (uint32_t)(m.sl(a2 + VY) + d3));
  }
  if (m.rb(a2 + AIR)) {
    int64_t vz = (int64_t)m.sl(a2 + VZ) - FALL_VZ;
    if (vz < FALL_VZ_MAX) { vz = FALL_VZ_MAX; }
    m.wl(a2 + VZ, (uint32_t)vz);
  }
  marble_physics::speedCap(m, a2);
}

// 0x15a12 init of a free steelie from spawn entry e: first node of its list, patrol, in the
// draw list
void init_15a12(Mem &m, uint32_t a2, const uint8_t *e) {
  const uint32_t nodes = be32(e + 2);
  m.wl(a2 + NODE, nodes);
  m.wl(a2 + NODES, nodes);
  m.wb(a2 + ZONE, e[6]);
  m.ww(a2 + BAND_LO, (uint16_t)(int16_t)s8(e[0]));
  m.ww(a2 + BAND_HI, (uint16_t)(int16_t)s8(e[1]));
  if (nodes == SPAWN_HI16) { m.ww(a2 + BAND_HI, m.rw(a2 + BAND_HI) - 0x10); }
  m.wl(a2 + PX, (uint32_t)(node(m, nodes, 0) * CELL + HALF_CELL));
  m.wl(a2 + PY, (uint32_t)(node(m, nodes, 1) * CELL + HALF_CELL));
  m.wl(a2 + PZ, (uint32_t)marble_physics::floorZ(m, a2));

  m.wl(a2 + VY, 0);
  m.wl(a2 + VX, 0);
  m.wb(a2 + SLOT, LIVE);
  m.wb(a2 + AIR, 0);
  m.wb(a2 + MODE, PATROL);
  m.wl(a2 + SCREEN_OLD, 0);
  m.wl(a2 + SCREEN, 0);
  m.ww(a2 + ANGER, NEVER_HIT);
  m.wb(a2 + SHARDS_ON, 0);
  m.wb(a2 + STUN_CNT, 0);
  m.wb(a2 + SURFACE, 0);
  for (uint8_t i = 0; i < MASK_N; i++) { m.ww(a2 + MASKS + MASK_ENTRY * i, 0); }
  marble_physics::project(m, a2, marble_physics::KEEP_CELL);
  marble_drawlist::insert(m, a2, TYPE_STEELIE);
}

// push of the pair collision: s8 0x24ad6[|v high word|] << 12, x4
// marble against steelie, away from the other object
int32_t push(const Mem &m, uint32_t obj, uint8_t o, uint32_t a2, uint32_t a3, uint8_t by,
             bool mixed) {
  int32_t v = (int32_t)marble_repel[(uint16_t)(abs64(m.sw(obj + o)) & 0xff)] * 0x1000;
  if (mixed) { v *= 4; }
  return m.sl(a2 + by) > m.sl(a3 + by) ? -v : v;
}

// 0x158f6: anger and stun timers, then the shatter animation (+0x18 == 2) or 0x253bc, AI
// 0x182ba, the object physics 0x121b8
void update_158f6(Mem &m, uint32_t a2) {
  if (!m.rb(a2 + SLOT)) { return; }
  const int16_t t = m.sw(a2 + ANGER);
  if (t > 0) {
    m.ww(a2 + ANGER, (uint16_t)(t - 1));
    if (t == 1 && (m.rb(a2 + MODE) == CHASE || m.rb(a2 + MODE) == HUNT)) { enter23_160d4(m, a2); }
  }

  if (m.rb(a2 + MODE) == STUN) {
    if (m.rb(a2 + STUN_CNT)) { m.wb(a2 + STUN_CNT, m.rb(a2 + STUN_CNT) - 1); }
    if (!m.rb(a2 + STUN_CNT)) { enter23_160d4(m, a2); }
  } else if (m.rb(a2 + SLOT) == SHATTERING) {
    // 0x1b9cc refetch flag: low byte of the steelie address, nonzero for both
    marble_physics::animate(m, a2);
    marble_physics::project(m, a2, marble_physics::REFETCH);
    marble_occlusion::enter(m, a2);
    return;
  }
  marble_physics::ground(m, a2);
  ai_182ba(m, a2);
  marble_physics::move(m, a2);
}

}  // namespace

void init(Mem &m) {
  for (uint8_t i = 0; i < NSTEELIES; i++) { m.wb(steelie(i) + IDX, i); }
  slinkyInit(m);
  acidInit(m);
  birdInit(m);
  miniInit(m);
}

// Spawn a steelie for every entry of 0x22706[race] whose band lo or hi is entered from
// outside [lo, hi] and whose node list no live steelie has (free slot: highest free index,
// none free ends the scan); then remove steelies whose band range is left.
void cameraBands(Mem &m, int16_t oldBand, int16_t newBand) {
  const int8_t a = s8((uint32_t)oldBand), b = s8((uint32_t)newBand);
  for (uint16_t i = marble_steelie_spawns_of[m.rw(RACE)].first; marble_steelie_spawns[i][0] != END; i++) {
    const uint8_t *e = marble_steelie_spawns[i];
    uint32_t free = 0;
    bool taken = false;
    for (uint8_t k = 0; k < NSTEELIES; k++) {
      const uint32_t s = steelie(k);
      if (!m.rb(s + SLOT)) {
        free = s;
      } else if (m.rl(s + NODES) == be32(e + 2)) {
        taken = true;
      }
    }
    if (!free) { break; }

    const int8_t lo = s8(e[0]), hi = s8(e[1]);
    if (!taken && (b == lo || b == hi) && !(lo <= a && a <= hi)) { init_15a12(m, free, e); }
  }

  for (uint8_t i = 0; i < NSTEELIES; i++) {
    const uint32_t s = steelie(i);
    if (!m.rb(s + SLOT)) { continue; }
    const int16_t lo = m.sw(s + BAND_LO), hi = m.sw(s + BAND_HI);
    if ((a == lo && b < lo) || (a == hi && b > hi)) { remove_15bd0(m, s, NO_SCORE, CLEAR); }
  }
  slinkyBands(m, a, b);
  acidBands(m, a, b);
}

void sillyBands(Mem &m, int16_t oldBand, int16_t newBand) {
  miniBands(m, s8((uint32_t)oldBand), s8((uint32_t)newBand));
  birdBands(m, s8((uint32_t)oldBand), s8((uint32_t)newBand));
}

void update(Mem &m) {
  for (uint8_t i = 0; i < NSTEELIES; i++) { update_158f6(m, steelie(i)); }
  slinkies_1493c(m);
  acid_17230(m);
  minis_1912c(m);
  birds_19baa(m);
  pistons_1844a(m);
}

// 0x1bc88: a2 against P1, P2 and (beginner, aerial, ultimate) the steelies. Overlap 7 px x/y,
// 14 px z, octant < 0x70: a2 back to its old position, velocities swapped, both pushed apart;
// a steelie gets angry and returns; a2 falling onto the other breaks it.
bool pair_1bc88(Mem &m, uint32_t a2) {
  const uint16_t race = m.rw(RACE);
  const uint8_t n = (race == 1 || race == 3 || race == 5) ? 4 : 2;
  const int16_t sx = m.sw(POS_X), sy = m.sw(POS_Y), sz = m.sw(POS_Z);
  bool hit = false;
  for (uint8_t i = 0; i < n; i++) {
    const uint32_t a3 = marble_pair_list[i];
    if (a3 == a2 || m.rb(a3 + SLOT) != LIVE) { continue; }
    const int16_t dx = s16(sx - m.rw(a3 + PX)), dy = s16(sy - m.rw(a3 + PY));
    if (dx < -PAIR_XY || dx > PAIR_XY || dy < -PAIR_XY || dy > PAIR_XY) { continue; }
    const int16_t dz = s16(sz - m.rw(a3 + PZ));
    if (dz < -PAIR_Z || dz > PAIR_Z) { continue; }
    const uint8_t st = m.rb(a3 + MODE);
    if (st == PLAYER_LOST || st == marble_physics::BROKEN || st == PLAYER_CARRIED ||
        m.rb(a2 + SURFACE) == SURFACE_TUBE) {
      continue;
    }
    if (octant(abs64(dx) << 16, abs64(dy) << 16) >= PAIR_DIST) { continue; }

    hit = true;
    const bool pa = marble_physics::isPlayer(a2), pb = marble_physics::isPlayer(a3);
    m.wl(a2 + PX, m.rl(OLD_X));
    m.wl(a2 + PY, m.rl(OLD_Y));
    m.wl(a2 + PZ, m.rl(OLD_Z));
    for (uint8_t o = VX; o <= VY; o += 4) {
      const uint32_t t = m.rl(a2 + o);
      m.wl(a2 + o, m.rl(a3 + o));
      m.wl(a3 + o, t);
    }

    const bool mixed = pa != pb;
    const int32_t ax = push(m, a2, VX, a2, a3, PX, mixed), ay = push(m, a2, VY, a2, a3, PY, mixed);
    m.wl(a2 + VX, (uint32_t)(m.sl(a2 + VX) - ax));
    m.wl(a2 + VY, (uint32_t)(m.sl(a2 + VY) - ay));
    const int32_t bx = push(m, a3, VX, a2, a3, PX, mixed), by = push(m, a3, VY, a2, a3, PY, mixed);
    m.wl(a3 + VX, (uint32_t)(m.sl(a3 + VX) + bx));
    m.wl(a3 + VY, (uint32_t)(m.sl(a3 + VY) + by));

    // steelies: angry the first time, stunned, back to the nearest node; sound 0x37 when two
    // players collide
    marble_sound::send(m, pa && pb ? marble_sound::SND_PLAYERS_HIT
                                   : marble_sound::SND_STEELIE_HIT);
    const uint32_t objs[] = {a2, a3};
    const bool players[] = {pa, pb};
    for (uint8_t k = 0; k < 2; k++) {
      if (players[k]) { continue; }
      const uint32_t o = objs[k];
      if (m.rw(o + ANGER) == NEVER_HIT) { m.ww(o + ANGER, ANGRY_TICKS); }
      m.wb(o + MODE, STUN);
      m.wb(o + STUN_CNT, m.rb(o + STUN_CNT) + STUN_HIT);
      enter23_160d4(m, o);
    }

    // a2 airborne and not below a3: a3 breaks
    if (dz < 0 || m.rb(a2 + AIR) == 0) { continue; }
    if (!pb) {
      remove_15bd0(m, a3, SCORE, KEEP);
      m.wb(a3 + SLOT, SHATTERING);
    }
    marble_physics::enterState(m, a3, marble_physics::BROKEN);
  }
  return hit;
}

// 0x25c74 non-player branch: stunned, +0x56 += fall; above 0x50 or a fall over 0x1f px the
// steelie shatters (scored)
void land_25c74(Mem &m, uint32_t a2, int16_t fall) {
  m.wb(a2 + MODE, STUN);
  int16_t d4 = (int16_t)(m.sb(a2 + DAMAGE) + fall);
  if (d4 > 0x7f) { d4 = 0x7f; }
  m.wb(a2 + DAMAGE, (uint8_t)d4);
  const uint8_t v = (uint8_t)(m.rb(a2 + STUN_CNT) + fall);
  m.wb(a2 + STUN_CNT, v);
  if (s8(v) <= STUN_BREAK && fall <= FALL_BREAK) { return; }

  remove_15bd0(m, a2, SCORE, KEEP);
  m.wb(a2 + SLOT, SHATTERING);
  marble_physics::enterState(m, a2, marble_physics::BROKEN);
}

// 0x15bd0: clear: free, draw list entry removed (0x18f46); score: 0x285b0(player, 3) for
// every player in play
void remove_15bd0(Mem &m, uint32_t a2, Score s, Clear c) {
  if (c == CLEAR) {
    m.wb(a2 + SLOT, 0);
    marble_drawlist::remove(m, a2, TYPE_STEELIE);
  }
  if (s == NO_SCORE) { return; }
  for (uint16_t i = 0; i < m.rw(PLAYERS); i++) {
    const uint32_t p = marble_physics::PLAYER + PLAYER_SIZE * i;
    const uint8_t slot = m.rb(p + SLOT);
    if (slot != 0 && slot != SHATTERING) { marble_physics::score(m, p, SCORE_STEELIE); }
  }
}

}  // namespace marble_enemies

#endif  // GAME_MARBLE
