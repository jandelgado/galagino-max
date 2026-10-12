#include "marble_drawlist.h"

#ifdef ENABLE_MARBLE

#include "../../emulation/asset.h"
#include "marble_enemies.h"
#include "marble_physics.h"
#include "marble_rom.h"

namespace marble_drawlist {

using marble_physics::Mem;
using marble_physics::s8;
using marble_physics::s16;
using namespace marble_video;

namespace {

static const uint32_t LIST = 0x4003bc, LIST_MAX = 0x1f;  // list bytes, entries
static const uint32_t ENTRIES = 0x4001dc, ENTRY_SIZE = 14, ENTRY_N = 31;
static const uint32_t NO_SORT = 0x4003e2;  // nonzero: boxes only, no sort passes
static const uint8_t END = 0xff;

static const uint32_t ROM_END = 0x30000;
static const uint32_t PLAYERS = 0x1eff6, ENEMIES = 0x1effe, ENEMIES4 = 0x1f006;
static const uint32_t SLOTS = 0x1f016, ENEMIES_E = 0x1f07a, ENEMIES7 = 0x1f096, ENEMIES_F = 0x1f0ba;
static const uint32_t TAG3_PAL = 0x1df18;     // tag 3 frame -> palette variant
static const uint8_t TAG3_FRAMES = 14;        // 7 frames of list 0x21192 follow the 7 of 0x21156
static const uint32_t TAG3_LIST2 = 0x21192;
static const uint32_t STRIP_LIST = 0x2227a;   // tag 6 list with wave strip entries
static const uint32_t FLAG_SET7 = 0x220ee;    // tag 6 headers from here use set 7
static const uint32_t NONE32 = 0xffffffff;
static const uint32_t TAG3_GLOBALS[3] = {0x40044a, 0x40044e, 0x400452};

enum Type : uint8_t {
  T_FREE = 0, T_PLAYER = 1, T_ENEMY = 2, T_TAG3 = 3, T_ENEMY4 = 4, T_TAG5 = 5, T_TAG6 = 6,
  T_ENEMY7 = 7, T_ENEMY9 = 9, T_ENEMY_E = 0xe, T_ENEMY_F = 0xf, T_SLOT_LAST = 0xd,
  T_29 = 0x29, T_2A = 0x2a, T_2C = 0x2c
};

enum Set : uint8_t { SET_FLAG = 3, SET_WAVE = 5, SET_LIFT = 6, SET_GLOBAL = 7, SET_SHADE = 8 };

// slot fields
enum : uint8_t { S_PX = 0x0c, S_PY = 0x10, S_PZ = 0x14, S_IDX = 0x19, S_TAG = 0x1f,
                 S_ANIM = 0x3e, S_FRAME = 0x42, S_ANIM2 = 0x46, S_SCREEN = 0x4e };

// ROM (< 0x30000) or work RAM, as the 68010 sees it; elsewhere 0
uint8_t rdb(const Mem &m, uint32_t a) {
  if (a < ROM_END) { return m.romb(a); }
  if (a - marble_physics::RAM_BASE < marble_physics::RAM_SIZE) { return m.rb(a); }
  return 0;
}

uint16_t rdw(const Mem &m, uint32_t a) { return rdb(m, a) << 8 | rdb(m, a + 1); }
uint32_t rdl(const Mem &m, uint32_t a) { return (uint32_t)rdw(m, a) << 16 | rdw(m, a + 2); }

inline uint32_t entry(uint8_t k) { return ENTRIES + ENTRY_SIZE * k; }

struct Box { int16_t v[6]; };  // x, y, z min, x, y, z max

// object box: position words +0x0c/+0x10/+0x14 plus offset d, size s
Box objBox(const Mem &m, uint32_t a, int8_t dx, int8_t dy, int8_t dz, int8_t sx, int8_t sy,
           int8_t sz) {
  const int16_t x = s16(m.rw(a + S_PX) + dx), y = s16(m.rw(a + S_PY) + dy);
  const int16_t z = s16(m.rw(a + S_PZ) + dz);
  return {{x, y, z, s16(x + sx), s16(y + sy), s16(z + sz)}};
}

struct Rect { int8_t dx, dy, sx, sy; };

// frame header bytes 4-7 of the box record p (0xffffffff reads ROM 3-6)
Rect rect(const Mem &m, uint32_t p) {
  return {s8(rdb(m, p + 4)), s8(rdb(m, p + 5)), s8(rdb(m, p + 6)), s8(rdb(m, p + 7))};
}

// 0x1b12a: box of an entry per type
Box box_1b12a(const Mem &m, uint8_t type, uint8_t idx) {
  switch (type) {
    case T_FREE: return {{0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff}};
    case T_PLAYER: return objBox(m, m.roml(PLAYERS + 4 * idx), -3, -3, 1, 6, 6, 6);
    case T_ENEMY: return objBox(m, m.roml(ENEMIES + 4 * idx), -3, -3, 1, 6, 6, 6);
    case T_ENEMY4:
    case T_ENEMY_E: {
      const uint32_t a2 = m.roml((type == T_ENEMY4 ? ENEMIES4 : ENEMIES_E) + 4 * idx);
      const uint32_t p = rdl(m, rdl(m, a2 + (type == T_ENEMY4 ? 0x58 : 0x3a)));
      const Rect r = p == NONE32 ? Rect{-4, -4, 8, 8} : rect(m, p);
      return type == T_ENEMY4 ? objBox(m, a2, r.dx, r.dy, 0, r.sx, r.sy, 0x10)
                              : objBox(m, a2, r.dx, r.dy, -8, r.sx, r.sy, 0);
    }
    case T_29: {
      const uint32_t a = 0x401650 + ((uint32_t)idx << 4);
      const int16_t x = s16((s8(m.rb(a + 4)) << 3) + 2), y = s16((s8(m.rb(a + 5)) << 3) + 2);
      const int16_t z = m.sw(a + 6);
      return {{x, y, z, s16(x + 4), s16(y + 4), s16(z + 0x14)}};
    }
    case T_2A: {
      const uint32_t a = 0x40098c + (uint32_t)idx * 12;
      const int16_t z = s16(m.rw(a + 4) - 8);
      return {{m.sw(a), m.sw(a + 2), z, m.sw(a), m.sw(a + 2), z}};
    }
    case T_2C: return {{0, 0, 0, 0, 0, 0}};
    case T_ENEMY7:
    case 8:
    case T_ENEMY9: {
      const uint32_t a1 = m.roml(ENEMIES7 + 4 * idx);
      const Rect r = rect(m, rdl(m, rdl(m, a1 + 0x1c)));
      const bool two = m.rb(a1 + 0x1a) == 2;
      return objBox(m, a1, r.dx, r.dy, two ? -8 : 0, r.sx, r.sy, two ? 0 : 6);
    }
    case T_ENEMY_F: {
      const uint32_t a1 = m.roml(ENEMIES_F + 4 * idx);
      if (m.rb(a1 + 0x1a) == 2) { return objBox(m, a1, 0, 0, 0, 0, 0, 0); }
      return objBox(m, a1, -4, -4, 0, 8, 8, 4);
    }
    default: break;
  }

  // level object slot, tag = type
  const uint32_t a1 = m.roml(SLOTS + 4 * idx);
  if (type < T_TAG3 || type > T_SLOT_LAST) { return objBox(m, a1, 0, 0, 0, 0, 0, 0); }

  const uint32_t p = rdl(m, rdl(m, a1 + S_ANIM));
  if (type == T_TAG6) {
    return objBox(m, a1, 0, s8(rdb(m, p + 4)), 0, 0x20, s8(rdb(m, p + 6)), 0x28);
  }

  if (type == 11 || type == 12 || type == 13) {
    if (p == NONE32) { return objBox(m, a1, 0, 0, -8, 8, 8, 0); }
    const Rect r = rect(m, p);
    return type == 12 ? objBox(m, a1, r.dx, r.dy, 0, r.sx, r.sy, 0x10)
                      : objBox(m, a1, r.dx, r.dy, -8, r.sx, r.sy, 0x18);
  }

  const Rect r = rect(m, p);
  if (type == T_TAG3) { return objBox(m, a1, r.dx, r.dy, -16, r.sx, r.sy, 0x10); }
  if (type == T_TAG5) { return objBox(m, a1, r.dx, r.dy, 0, r.sx, r.sy, 0x18); }
  return objBox(m, a1, r.dx, r.dy, -8, r.sx, r.sy, 0);  // 10
}

Box entryBox(const Mem &m, uint32_t e) {
  Box b;
  for (uint8_t i = 0; i < 6; i++) { b.v[i] = m.sw(e + 2 + 2 * i); }
  return b;
}

// 0x1a80a: true when p must be drawn after q
bool after_1a80a(const Box &p, const Box &q) {
  const int32_t pmax = p.v[3] + p.v[4] + p.v[5], pmin = p.v[0] + p.v[1] + p.v[2];
  const int32_t qmax = q.v[3] + q.v[4] + q.v[5], qmin = q.v[0] + q.v[1] + q.v[2];
  if (pmax <= qmin) { return false; }
  if (qmax <= pmin) { return true; }
  if (q.v[1] >= p.v[4]) { return false; }
  if (p.v[1] >= q.v[4]) { return true; }
  if (q.v[0] >= p.v[3]) { return false; }
  if (p.v[0] >= q.v[3]) { return true; }
  if (q.v[2] >= p.v[5]) { return false; }
  return true;
}

// list bytes before the end marker, at most LIST_MAX
uint8_t listLen(const Mem &m) {
  uint8_t n = 0;
  while (n < LIST_MAX && m.rb(LIST + n) != END) { n++; }
  return n;
}

// 0x1a7a8: one pass of adjacent swaps at distance stride
void pass_1a7a8(Mem &m, uint8_t stride) {
  for (uint8_t i = 0; i < 0x20 - stride; i++) {
    const uint32_t a = LIST + i, b = LIST + i + stride;
    if (m.rb(a) == END || m.rb(b) == END) { return; }
    if (after_1a80a(entryBox(m, entry(m.rb(a))), entryBox(m, entry(m.rb(b))))) {
      const uint8_t t = m.rb(a);
      m.wb(a, m.rb(b));
      m.wb(b, t);
    }
  }
}

void putBox(Mem &m, uint32_t e, const Box &b) {
  for (uint8_t i = 0; i < 6; i++) { m.ww(e + 2 + 2 * i, (uint16_t)b.v[i]); }
}

// object list and mask pool under construction; n counts past OBJS, the caller truncates
struct Out {
  VideoState &v;
  uint8_t n, nm;  // objects, pool entries
  void add(uint8_t frame, uint8_t set, int16_t x, int16_t y) {
    if (n < OBJS) { v.objs[n] = {frame, set, x, y, nm, 0}; }
    n++;
  }
  // the last object's nmask, nullptr if it was dropped
  uint8_t *last() { return n && n <= OBJS ? &v.objs[n - 1].nmask : nullptr; }
  // mask of the owner with mask count *count; dropped when the pool is full
  void mask(uint8_t *count, uint16_t code, int16_t dx, int16_t dy) {
    if (!count || nm >= MASKS) { return; }
    v.masks[nm++] = {code, dx, dy};
    (*count)++;
  }
};

void one(const Mem &m, Out &o, uint32_t hdr, uint8_t set, int16_t x, int16_t y) {
  if (hdr != NONE32) { o.add(m.spriteFrame(hdr), set, x, y); }
}

inline uint32_t hdrOf(const Mem &m, uint32_t p) { return rdl(m, rdl(m, p)); }

// slot objects of 0x27a16 / 0x27dd0 / 0x27ed4 (tags 3, 5, 6); lift = palette variant if drawn
void slotObjs(const Mem &m, Out &o, uint8_t type, uint8_t idx, int32_t top, int16_t margin,
              uint8_t &lift) {
  static const int16_t LO = -0x40;                 // sy + 0x10 above this is culled
  static const int16_t HI_TAG5 = 0x100, HI_TAG3 = 0x160, HI_TAG6 = 0x140;
  static const int16_t SCREEN_H = 240;
  static const uint16_t X_BIAS = 0x18, Y_BIAS = 0x10;

  const uint32_t a = m.roml(SLOTS + 4 * idx);
  const uint16_t sxw = m.rw(a + S_SCREEN) + (type == T_TAG5 ? X_BIAS - 1 : X_BIAS);
  const uint16_t syw = m.rw(a + S_SCREEN + 2);
  const int16_t x = s16(sxw) - X_BIAS;
  const int16_t y = (int16_t)(top + SCREEN_H - s16(syw));
  const int16_t cy = s16((uint16_t)(syw + Y_BIAS));

  if (type == T_TAG5) {
    if (cy <= LO - margin || cy >= HI_TAG5 + margin) { return; }
    one(m, o, hdrOf(m, a + S_FRAME), SET_FLAG, x, y);
    return;
  }

  if (type == T_TAG3) {
    if (cy <= LO - margin || cy >= HI_TAG3 + margin) { return; }
    for (uint8_t g = 0; g < 3; g++) { one(m, o, hdrOf(m, TAG3_GLOBALS[g]), SET_GLOBAL, x, y); }
    const uint32_t own = hdrOf(m, a + S_FRAME);
    one(m, o, own, SET_LIFT, x, y);
    // 32 bit: += 7 does not wrap at 16 bits (0xffff + 7 is no frame)
    uint32_t i = ((int32_t)(m.rl(a + S_FRAME) - m.rl(a + S_ANIM2)) >> 3) & 0xffff;
    if (m.rl(a + S_ANIM2) == TAG3_LIST2) { i += 7; }
    if (own != NONE32 && i < TAG3_FRAMES) { lift = m.romb(TAG3_PAL + i); }
    return;
  }

  if (cy <= LO - margin || cy >= HI_TAG6 + margin) { return; }
  const uint32_t p42 = m.rl(a + S_FRAME), p46 = m.rl(a + S_ANIM2);
  const uint32_t hdr = rdl(m, p42);
  one(m, o, hdr, hdr >= FLAG_SET7 ? SET_GLOBAL : SET_WAVE, x, y);
  if (p46 != STRIP_LIST) { return; }

  const int16_t i = s16((uint32_t)((int32_t)(p42 - p46) >> 2) & 0xffff);
  const int16_t f = m.stripFrame(i);
  if (f >= 0) { o.add((uint8_t)f, SET_SHADE, x, y); }
}

// player 1 (and steelie) fields
enum : uint8_t { P_SLOT = 0x18, P_STATE = 0x1a, P_VISIBLE = 0x1c, P_SX = 0x1e, P_SY = 0x20,
                 P_MASKS = 0x38, P_ANIM = 0x5a, P_SHARDS = 0x62, P_SHARDS_ON = 0x67,
                 P_STARS = 0xa4 };
static const uint8_t ENTRY = 6, MASK_N = 5, STAR_N = 4;  // {code, x, y} entries
static const uint16_t MASK_BIT = 0x4000;                 // mask entry, else sparkle star
static const uint8_t ST_DISSOLVE = 9;

// player state drawn from the animation frame +0x5a
inline bool frameState(uint8_t st) {
  return st == marble_physics::DIZZY || st == marble_physics::BROKEN ||
         st == marble_physics::EATEN || st == ST_DISSOLVE;
}

// sparkle star entry (0x13ade: code 0x10b + k, screen x, y) as a one tile object
void star(Out &o, uint16_t code, int16_t x, int16_t y, int32_t top) {
  const uint16_t k = (code & 0x7ff) - MARBLE_STAR_CODE;
  if (k >= MARBLE_STARS) { return; }
  o.add((uint8_t)k, SET_DEATH | SET_STAR, x, (int16_t)(top + ARCADE_H - y - 8));
}

// 0x2724c (type 1) of player 1 at origin (sx, top + ARCADE_H - sy): the marble with its masks,
// or the death frame with its masks, sparkle stars (+0xa4, star entries of +0x38; LOST,
// CARRIED and CAUGHT) and the shatter overlay as objects. Dots 0x1c014 are not ported.
void player(const Mem &m, Out &o, int32_t top) {
  static const uint32_t A = marble_physics::PLAYER;
  VideoState &v = o.v;
  v.marble = HIDDEN;
  v.mask = o.nm;
  v.nmask = 0;
  if (!m.rb(A + P_VISIBLE)) { return; }

  const uint8_t st = m.rb(A + P_STATE);
  const int16_t sx = m.sw(A + P_SX), sy = m.sw(A + P_SY);
  const int16_t y = (int16_t)(top + ARCADE_H - sy);
  uint8_t *owner = nullptr;  // mask count of the masks' owner
  int16_t ox = 0, oy = 0;    // its origin relative to the player origin
  if (frameState(st)) {
    const uint32_t hdr = hdrOf(m, A + P_ANIM);
    if (hdr != NONE32) {
      o.add(marble_physics::deathFrame(hdr), SET_DEATH, sx, y);
      owner = o.last();
    }
  } else if (st == marble_physics::LOST || st == marble_physics::CARRIED ||
             st == marble_physics::CAUGHT) {
    for (uint8_t i = 0; i < STAR_N && m.rw(A + P_STARS + ENTRY * i); i++) {
      const uint32_t e = A + P_STARS + ENTRY * i;
      star(o, m.rw(e), m.sw(e + 2), m.sw(e + 4), top);
    }
  } else {
    v.marble = SHOWN;
    owner = &v.nmask;
    ox = MARBLE_X;
    oy = MARBLE_Y;
  }

  // masks follow the drawn part (MO top = ARCADE_H - y - 8 h, MARBLE.md "Occlusion")
  for (uint8_t i = 0; i < MASK_N && m.rw(A + P_MASKS + ENTRY * i); i++) {
    const uint32_t e = A + P_MASKS + ENTRY * i;
    const uint16_t code = m.rw(e);
    const int16_t ex = m.sw(e + 2), ey = m.sw(e + 4);
    if (!(code & MASK_BIT)) {
      star(o, code, ex, ey, top);
      continue;
    }
    const int16_t h = (((code >> 11) & 7) + 1) * 8;
    o.mask(owner, code, (int16_t)(ex - sx - ox), (int16_t)(sy - ey - h - oy));
  }

  if (!m.rb(A + P_SHARDS_ON)) { return; }
  const uint32_t hdr = hdrOf(m, A + P_SHARDS);
  if (hdr != NONE32) { o.add(marble_physics::deathFrame(hdr), SET_DEATH | SET_SHARDS, sx, y); }
}

// 0x2724c type 2 (steelie, Marble Love late-game-logic-26f3e.ts) at origin (sx, top + ARCADE_H
// - sy), drawn while +0x1c: frame 0x21f36 (palette set 5) or, shattering, the death frame
// rl(+0x5a) in set 5, its masks +0x38, the shatter overlay rl(+0x62) while +0x67
void steelie(const Mem &m, Out &o, uint8_t idx, int32_t top) {
  static const uint32_t FRAME = 0x21f36;
  static const uint8_t SHATTERING = 2, SET_STEELIE = 5;
  const uint32_t a = m.roml(ENEMIES + 4 * idx);
  if (!m.rb(a + P_VISIBLE)) { return; }

  const int16_t sx = m.sw(a + P_SX), sy = m.sw(a + P_SY);
  const int16_t y = (int16_t)(top + ARCADE_H - sy);
  if (m.rb(a + P_SLOT) == SHATTERING) {
    const uint32_t hdr = hdrOf(m, a + P_ANIM);
    if (hdr == NONE32) { return; }
    o.add(marble_physics::deathFrame(hdr), SET_DEATH | SET_RACE5, sx, y);
  } else {
    o.add(m.spriteFrame(FRAME), SET_STEELIE, sx, y);
  }

  uint8_t *owner = o.last();
  for (uint8_t i = 0; i < MASK_N && m.rw(a + P_MASKS + ENTRY * i); i++) {
    const uint32_t e = a + P_MASKS + ENTRY * i;
    const uint16_t code = m.rw(e);
    const int16_t h = (((code >> 11) & 7) + 1) * 8;
    o.mask(owner, code, (int16_t)(m.sw(e + 2) - sx), (int16_t)(sy - m.sw(e + 4) - h));
  }

  if (!m.rb(a + P_SHARDS_ON)) { return; }
  const uint32_t hdr = hdrOf(m, a + P_SHARDS);
  if (hdr != NONE32) { o.add(marble_physics::deathFrame(hdr), SET_DEATH | SET_SHARDS, sx, y); }
}

// 0x2724c type 4 (slinky, late-game-logic-26f3e.ts) at origin (+0x28, top + ARCADE_H - +0x2a),
// culled as the arcade (-0x20 < sy + 0x10 < 0x100, widened by margin): the death set frame
// rl(+0x58) in the slinky palette, its masks +0x2c
void slinky(const Mem &m, Out &o, uint8_t idx, int32_t top, int16_t margin) {
  static const int16_t LO = -0x20, HI = 0x100, Y_BIAS = 0x10;
  static const uint8_t SCREEN = 0x28, MASKS = 0x2c, CURSOR = 0x58;
  const uint32_t a = m.roml(ENEMIES4 + 4 * idx);
  const int16_t sx = m.sw(a + SCREEN), sy = m.sw(a + SCREEN + 2);
  const int16_t cy = s16((uint16_t)(sy + Y_BIAS));
  if (cy <= LO - margin || cy >= HI + margin) { return; }
  const uint32_t hdr = hdrOf(m, a + CURSOR);
  if (hdr == NONE32) { return; }
  o.add(marble_physics::deathFrame(hdr), SET_DEATH | SET_SLINKY, sx, (int16_t)(top + ARCADE_H - sy));

  uint8_t *owner = o.last();
  for (uint8_t i = 0; i < MASK_N && m.rw(a + MASKS + ENTRY * i); i++) {
    const uint32_t e = a + MASKS + ENTRY * i;
    const uint16_t code = m.rw(e);
    const int16_t h = (((code >> 11) & 7) + 1) * 8;
    o.mask(owner, code, (int16_t)(m.sw(e + 2) - sx), (int16_t)(sy - m.sw(e + 4) - h));
  }
}

// 0x2724c type 0x0e (acid, late-game-logic-26f3e.ts) at origin (+0x28, top + ARCADE_H - +0x2a),
// culled as the arcade (-0x30 < sy + 0x10 < 0x120, widened by margin): the death set frame
// rl(+0x3a) in the acid palette, no masks
void acid(const Mem &m, Out &o, uint8_t idx, int32_t top, int16_t margin) {
  static const int16_t LO = -0x30, HI = 0x120, Y_BIAS = 0x10;
  static const uint8_t SCREEN = 0x28, CURSOR = 0x3a;
  const uint32_t a = m.roml(ENEMIES_E + 4 * idx);
  const int16_t sx = m.sw(a + SCREEN), sy = m.sw(a + SCREEN + 2);
  const int16_t cy = s16((uint16_t)(sy + Y_BIAS));
  if (cy <= LO - margin || cy >= HI + margin) { return; }
  const uint32_t hdr = hdrOf(m, a + CURSOR);
  if (hdr == NONE32) { return; }
  o.add(marble_physics::deathFrame(hdr), SET_DEATH | SET_ACID, sx, (int16_t)(top + ARCADE_H - sy));
}

// 0x2724c types 7/8/9 (silly race minis, late-game-logic-26f3e.ts) at origin (+0x20, top +
// ARCADE_H - +0x22), culled as the arcade (-0x10 < sy + 0x10 < 0x100, widened by margin): the
// death set frame rl(+0x1c) in the mini palette of its type, no masks
void mini(const Mem &m, Out &o, uint8_t type, uint8_t idx, int32_t top, int16_t margin) {
  static const int16_t LO = -0x10, HI = 0x100, Y_BIAS = 0x10;
  static const uint8_t CURSOR = 0x1c, SCREEN = 0x20;
  const uint32_t a = m.roml(ENEMIES7 + 4 * idx);
  const int16_t sx = m.sw(a + SCREEN), sy = m.sw(a + SCREEN + 2);
  const int16_t cy = s16((uint16_t)(sy + Y_BIAS));
  if (cy <= LO - margin || cy >= HI + margin) { return; }
  const uint32_t hdr = hdrOf(m, a + CURSOR);
  if (hdr == NONE32) { return; }
  const uint8_t set = SET_DEATH | (uint8_t)(SET_MINI + type - T_ENEMY7);
  o.add(marble_physics::deathFrame(hdr), set, sx, (int16_t)(top + ARCADE_H - sy));
}

// 0x2724c type 0x0f (silly race bird, late-game-logic-26f3e.ts) at origin (+0x20, top +
// ARCADE_H - +0x22), culled as the arcade (-0x10 < sy + 0x10 < 0x100, widened by margin): the
// death set frame rl(+0x1c) in the bird palette, its masks +0x26
void bird(const Mem &m, Out &o, uint8_t idx, int32_t top, int16_t margin) {
  static const int16_t LO = -0x10, HI = 0x100, Y_BIAS = 0x10;
  static const uint8_t CURSOR = 0x1c, SCREEN = 0x20, MASKS = 0x26, NMASKS = 3;
  const uint32_t a = m.roml(ENEMIES_F + 4 * idx);
  const int16_t sx = m.sw(a + SCREEN), sy = m.sw(a + SCREEN + 2);
  const int16_t cy = s16((uint16_t)(sy + Y_BIAS));
  if (cy <= LO - margin || cy >= HI + margin) { return; }
  const uint32_t hdr = hdrOf(m, a + CURSOR);
  if (hdr == NONE32) { return; }
  o.add(marble_physics::deathFrame(hdr), SET_DEATH | SET_BIRD, sx, (int16_t)(top + ARCADE_H - sy));

  uint8_t *owner = o.last();
  for (uint8_t i = 0; i < NMASKS && m.rw(a + MASKS + ENTRY * i); i++) {
    const uint32_t e = a + MASKS + ENTRY * i;
    const uint16_t code = m.rw(e);
    const int16_t h = (((code >> 11) & 7) + 1) * 8;
    o.mask(owner, code, (int16_t)(m.sw(e + 2) - sx), (int16_t)(sy - m.sw(e + 4) - h));
  }
}

// 0x2724c type 0x29 (aerial race piston, late-game-logic-26f3e.ts) at origin (+0xc, top +
// ARCADE_H - +0xe), culled as the arcade (-0x40 < sy + 0x10 < 0x100, widened by margin): the
// death set frame rl(+8) in the piston palette, no masks
void piston(const Mem &m, Out &o, uint8_t idx, int32_t top, int16_t margin) {
  static const int16_t LO = -0x40, HI = 0x100, Y_BIAS = 0x10;
  static const uint8_t CURSOR = 0x08, SCREEN = 0x0c;
  const uint32_t a = marble_enemies::PISTONS + marble_enemies::PISTON_SIZE * idx;
  const int16_t sx = m.sw(a + SCREEN), sy = m.sw(a + SCREEN + 2);
  const int16_t cy = s16((uint16_t)(sy + Y_BIAS));
  if (cy <= LO - margin || cy >= HI + margin) { return; }
  const uint32_t hdr = hdrOf(m, a + CURSOR);
  if (hdr == NONE32) { return; }
  o.add(marble_physics::deathFrame(hdr), SET_DEATH | SET_PISTON, sx, (int16_t)(top + ARCADE_H - sy));
}

}  // namespace

// 0x28ca6 list part: entries free, list empty; the player (type 1) is the first entry
void init(Mem &m) {
  for (uint32_t k = 0; k < ENTRY_N; k++) { m.wb(entry(k), T_FREE); }
  for (uint32_t k = 0; k < ENTRY_N; k++) { m.wb(LIST + k, END); }
  insert(m, marble_physics::PLAYER, T_PLAYER);
}

// 0x18e6c: new entry before the first listed entry that must be drawn after it, else last.
// Only type and index are stored, the box is refreshed by the next sort().
void insert(Mem &m, uint32_t obj, uint8_t type) {
  const uint8_t idx = m.rb(obj + S_IDX);
  const Box box = box_1b12a(m, type, idx);

  uint32_t at = 0;
  while (at < LIST_MAX && m.rb(LIST + at) != END &&
         !after_1a80a(entryBox(m, entry(m.rb(LIST + at))), box)) {
    at++;
  }
  if (at >= LIST_MAX) { return; }

  uint8_t k = 0;
  while (k < ENTRY_N && m.rb(entry(k)) != T_FREE) { k++; }
  if (k >= ENTRY_N) { return; }
  m.wb(entry(k), type);
  m.wb(entry(k) + 1, idx);

  // open a gap at `at`; a full list drops its last byte
  uint32_t last = at;
  while (last < LIST_MAX && m.rb(LIST + last) != END) { last++; }
  for (uint32_t i = last; i > at; i--) {
    if (i < LIST_MAX) { m.wb(LIST + i, m.rb(LIST + i - 1)); }
  }
  m.wb(LIST + at, k);
}

// 0x18f46: entry of the object freed, later list bytes move up, END after the last
void remove(Mem &m, uint32_t obj) { remove(m, obj, m.rb(obj + S_TAG)); }

void remove(Mem &m, uint32_t obj, uint8_t type) {
  const uint8_t idx = m.rb(obj + S_IDX);
  uint32_t at = 0;
  for (; at < LIST_MAX && m.rb(LIST + at) != END; at++) {
    const uint32_t e = entry(m.rb(LIST + at));
    if (m.rb(e) == type && m.rb(e + 1) == idx) { break; }
  }
  if (at >= LIST_MAX || m.rb(LIST + at) == END) { return; }

  m.wb(entry(m.rb(LIST + at)), T_FREE);
  uint32_t last = at + 1;
  while (last < LIST_MAX && m.rb(LIST + last) != END) { last++; }
  for (uint32_t i = at; i + 1 < last; i++) { m.wb(LIST + i, m.rb(LIST + i + 1)); }
  m.wb(LIST + last - 1, END);
}

// 0x26f3e phases 1-2
void sort(Mem &m) {
  const uint8_t n = listLen(m);
  for (uint8_t i = 0; i < n; i++) {
    const uint32_t e = entry(m.rb(LIST + i));
    putBox(m, e, box_1b12a(m, m.rb(e), m.rb(e + 1)));
  }
  if (m.rb(NO_SORT)) { return; }
  for (uint8_t stride = 1; stride <= 3; stride++) { pass_1a7a8(m, stride); }
}

uint8_t build(const Mem &m, int16_t cam, int16_t cullMargin, VideoState &v) {
  static const uint8_t NOT_SEEN = 0xff;
  const int32_t top = (int32_t)cam - (int16_t)m.romw(m.desc() + marble_physics::DESC_TOP);
  Out o = {v, 0, 0};
  v.lift = LIFT_NONE;
  v.marbleAt = NOT_SEEN;
  v.marble = HIDDEN;
  v.nmask = 0;

  const uint8_t n = listLen(m);
  for (uint8_t i = 0; i < n; i++) {
    const uint32_t e = entry(m.rb(LIST + i));
    const uint8_t type = m.rb(e), idx = m.rb(e + 1);
    if (type == T_PLAYER && idx == 0 && v.marbleAt == NOT_SEEN) {
      v.marbleAt = o.n < OBJS ? o.n : OBJS;
      player(m, o, top);
    }
    if (type == T_TAG3 || type == T_TAG5 || type == T_TAG6) {
      slotObjs(m, o, type, idx, top, cullMargin, v.lift);
    }
    if (type == T_ENEMY) { steelie(m, o, idx, top); }
    if (type == T_ENEMY4) { slinky(m, o, idx, top, cullMargin); }
    if (type == T_ENEMY_E) { acid(m, o, idx, top, cullMargin); }
    if (type == T_ENEMY_F) { bird(m, o, idx, top, cullMargin); }
    if (type >= T_ENEMY7 && type <= T_ENEMY9) { mini(m, o, type, idx, top, cullMargin); }
    if (type == T_29) { piston(m, o, idx, top, cullMargin); }
  }
  v.n = o.n < OBJS ? o.n : OBJS;
  if (v.marbleAt > v.n) { v.marbleAt = v.n; }
  return o.n;
}

}  // namespace marble_drawlist

#endif  // ENABLE_MARBLE
