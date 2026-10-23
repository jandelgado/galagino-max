#include "marble_acid.h"
#include "marble_enemies.h"
#include "marble_drawlist.h"
#include "marble_physics.h"
#include "marble_rng.h"
#include "marble_sound.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

// 1:1 port of dev/marble/marble_acid.py (function names as there; Marble Love
// string-range-dispatch-17346.ts, string-step-1725a.ts, entity-waypoint-step-1d1ec.ts,
// sub-1d242.ts, sprite-coords.ts 0x1778e, string-viewport-hit-175c8.ts,
// string-target-step-176d2.ts). Integer semantics follow the model.
//
// An acid crawls its path one cell (8 px) per animation loop: on a waypoint cell the path
// cursor moves on, then it aims one axis step at the cursor's cell. A marble touching it
// dissolves (player state DISSOLVING, then LOST).

// everything in its own namespace: the host tests compile every module into one unit, where
// same-named constants of the other enemy files would hide these
namespace marble_acid {

using marble_physics::Mem;
using marble_physics::s8;
using marble_physics::s16;
using marble_enemies::ACIDS;
using marble_enemies::ACID_SIZE;
using marble_enemies::NACIDS;

namespace {

// acid fields (MARBLE.md "Acid in the main tick model")
enum : uint8_t {
  VX = 0x00, VY = 0x04, PX = 0x0c, PY = 0x10, PZ = 0x14,
  SLOT = 0x18, IDX = 0x19, STATE = 0x1a, ZONE = 0x1b,
  TICK = 0x24, TICKS = 0x25,       // frame ticker, ticks per frame
  SCREEN = 0x28,                   // sx, sy words
  CURSOR = 0x2c, PATH = 0x30,      // longs: path cursor, path (4 byte records)
  BAND_LO = 0x34, BAND_HI = 0x36,
  FRAME = 0x3a, ANIM = 0x3e,       // longs: frame cursor, animation list
};

// player fields
enum : uint8_t { P_SLOT = 0x18, P_ZONE = 0x1b, P_ACID = 0x58 };

static const uint8_t LIVE = 1, TYPE_ACID = 0xe;   // draw list type
static const uint8_t SPAWN_ENTRY = 8, WAYPOINT = 4, END = 0xff;
static const uint8_t KIND_SINGLE = 5, GROUP_N = 6;  // spawn entry zone byte 5: one acid
static const uint8_t FAST_ZONE = 6;                 // zone where a rolling player speeds it up
static const uint8_t TICKS_SLOW = 2, TICKS_FAST = 1;
static const uint8_t HIT_TICKS = 0x1c;              // frame held after dissolving a marble

// work RAM
static const uint32_t PLAYERS = 0x400396;
static const uint32_t POS_X = 0x400690, POS_Y = 0x400692;  // words
static const uint16_t PLAYER_SIZE = 0xe2;

// program ROM
static const uint32_t SPAWN_TABLE = 0x23d4a;           // per race: 8 byte entries, 0xff end
static const uint32_t SINGLE_A = 0x23d62, SINGLE_B = 0x23d66;  // random path lists (rng(2))
static const uint32_t GROUP_A = 0x23d6a, GROUP_B = 0x23d82;
static const uint32_t NO_FRAME = 0xffffffff;

// animation lists (0x1d242)
static const uint32_t ANIM_DOWN = 0x20ec4, ANIM_UP = 0x20ee4;
static const uint32_t ANIM_RIGHT = 0x20ea4, ANIM_LEFT = 0x20f24;

static const int32_t CELL = 0x80000;            // 8 px cell, 16.16
static const int8_t AXIS_STEP = 8;              // crawl step (px per anim loop) << 16
static const int16_t BOX_XY = 3;                // marble box +-3
static const int8_t BOX_DEFAULT = -2, BOX_DEFAULT_SIZE = 12;

uint32_t acid(uint8_t i) { return ACIDS + ACID_SIZE * i; }

int16_t cell(uint32_t v) { return s16((uint32_t)((int32_t)v >> 19)); }

// 0x1778e: screen position +0x28 as slot objects (no masks)
void coords_1778e(Mem &m, uint32_t a2) {
  const uint16_t x = m.rw(a2 + PX), y = m.rw(a2 + PY);
  m.ww(POS_X, x);
  m.ww(POS_Y, y);
  const uint16_t sx = (uint16_t)(y - x + 0x88);
  const uint16_t sy = (uint16_t)(m.rw(marble_physics::CAMERA) + m.rw(a2 + PZ) + 0x54 -
                                 (uint16_t)(((int32_t)s16(x) + s16(y)) >> 1));
  m.wl(a2 + SCREEN, (uint32_t)sx << 16 | sy);
}

// one axis step of 8 towards t (0 when there)
int8_t towards(int16_t c, int16_t t) { return c > t ? -AXIS_STEP : (c < t ? AXIS_STEP : 0); }

// a live player rolls in the acid's zone, and that zone is 6
bool fastZone(const Mem &m, uint32_t a2) {
  for (uint16_t i = 0; i < m.rw(PLAYERS); i++) {
    const uint32_t o = marble_physics::PLAYER + PLAYER_SIZE * i;
    if (m.rb(o + P_SLOT) == LIVE && m.rb(a2 + ZONE) == m.rb(o + P_ZONE) &&
        m.rb(o + P_ZONE) == FAST_ZONE) {
      return true;
    }
  }
  return false;
}

// 0x1d242: one axis step of 8 px towards the path cursor's cell (y first while moving in x),
// crawl animation, ticker reset
void aim_1d242(Mem &m, uint32_t a2) {
  const int16_t cx = cell(m.rl(a2 + PX)), cy = cell(m.rl(a2 + PY));
  const uint32_t p = m.rl(a2 + CURSOR);
  const int16_t tx = s8(m.romb(p)), ty = s8(m.romb(p + 1));
  int8_t dx = 0, dy = 0;
  if (m.rl(a2 + VX)) {
    dy = towards(cy, ty);
    if (!dy) { dx = towards(cx, tx); }
  } else {
    dx = towards(cx, tx);
    if (!dx) { dy = towards(cy, ty); }
  }
  const uint32_t anim = dy > 0 ? ANIM_DOWN : dy < 0 ? ANIM_UP : dx > 0 ? ANIM_RIGHT : ANIM_LEFT;
  m.wl(a2 + VX, (uint32_t)((int32_t)dx * 0x10000));
  m.wl(a2 + VY, (uint32_t)((int32_t)dy * 0x10000));
  m.wl(a2 + ANIM, anim);
  m.wl(a2 + FRAME, anim);
  m.wb(a2 + TICK, 0);
  m.wb(a2 + TICKS, fastZone(m, a2) ? TICKS_FAST : TICKS_SLOW);
}

// 0x1d1ec: on the path cursor's cell the cursor moves to path + next * 4; then re-aim
void path_1d1ec(Mem &m, uint32_t a2) {
  const uint32_t p = m.rl(a2 + CURSOR);
  if (cell(m.rl(a2 + PX)) == s8(m.romb(p)) && cell(m.rl(a2 + PY)) == s8(m.romb(p + 1))) {
    m.wl(a2 + CURSOR, m.rl(a2 + PATH) + (uint32_t)(s8(m.romb(p + 2)) * WAYPOINT));
  }
  aim_1d242(m, a2);
}

// 0x17346 init: at the first cell of path (cell corner), in the draw list
void init_17346(Mem &m, uint32_t a2, uint32_t e, uint32_t path) {
  m.wl(a2 + CURSOR, path);
  m.wl(a2 + PATH, path);
  m.wb(a2 + ZONE, m.romb(e + 6));
  m.ww(a2 + BAND_LO, (uint16_t)(int16_t)s8(m.romb(e)));
  m.ww(a2 + BAND_HI, (uint16_t)(int16_t)s8(m.romb(e + 1)));
  m.wl(a2 + PX, (uint32_t)(s8(m.romb(path)) * CELL));
  m.wl(a2 + PY, (uint32_t)(s8(m.romb(path + 1)) * CELL));
  m.wl(a2 + PZ, (uint32_t)marble_physics::cellHeight(m, a2));

  m.wl(a2 + VY, 0);
  m.wl(a2 + VX, 0);
  m.wb(a2 + SLOT, LIVE);
  m.wb(a2 + STATE, 0);
  m.wl(a2 + SCREEN, 0);
  path_1d1ec(m, a2);
  coords_1778e(m, a2);
  marble_drawlist::insert(m, a2, TYPE_ACID);
}

// highest free slot, 0 if none
uint32_t freeSlot(const Mem &m) {
  uint32_t free = 0;
  for (uint8_t i = 0; i < NACIDS; i++) {
    if (!m.rb(acid(i) + SLOT)) { free = acid(i); }
  }
  return free;
}

// 0x1730c: a live acid has this path
bool taken(const Mem &m, uint32_t path) {
  for (uint8_t i = 0; i < NACIDS; i++) {
    if (m.rb(acid(i) + SLOT) && m.rl(acid(i) + PATH) == path) { return true; }
  }
  return false;
}

// spawn entry e: its path, or (path 0) one or six paths of a random list; false when the
// slots ran out (ends the entry walk)
bool spawn(Mem &m, uint32_t e, uint32_t path) {
  if (path) {
    const uint32_t a = freeSlot(m);
    if (!a) { return false; }
    init_17346(m, a, e, path);
    return true;
  }

  const bool single = m.romb(e + 6) == KIND_SINGLE;
  const bool other = marble_rng::rng(m, 2) != 0;
  const uint32_t t = single ? (other ? SINGLE_B : SINGLE_A) : (other ? GROUP_B : GROUP_A);
  for (uint8_t k = 0; k < (single ? 1 : GROUP_N); k++) {
    const uint32_t a = freeSlot(m);
    if (!a) { return false; }
    init_17346(m, a, e, m.roml(t + 4 * k));
  }
  return true;
}

// 0x1725a: frame ticker; at the animation list end the acid steps by v and follows its path
void tick_1725a(Mem &m, uint32_t a2) {
  if (!m.rb(a2 + SLOT)) { return; }

  m.wb(a2 + TICK, m.rb(a2 + TICK) + 1);
  if (m.sb(a2 + TICKS) <= m.sb(a2 + TICK)) {
    m.wb(a2 + TICK, 0);
    if (m.sb(a2 + TICKS) > TICKS_SLOW) { m.wb(a2 + TICKS, TICKS_FAST); }
    const uint32_t p = m.rl(a2 + FRAME) + 4;
    m.wl(a2 + FRAME, p);
    if (m.roml(p) == NO_FRAME) {
      m.wl(a2 + FRAME, m.rl(a2 + ANIM));
      m.wl(a2 + PX, m.rl(a2 + PX) + m.rl(a2 + VX));
      m.wl(a2 + PY, m.rl(a2 + PY) + m.rl(a2 + VY));
      path_1d1ec(m, a2);
    }
  }
  coords_1778e(m, a2);
}

// contact box offsets of an acid's current frame (header bytes 4-7)
void box(const Mem &m, uint32_t a2, int16_t b[4]) {
  const uint32_t f = m.roml(m.rl(a2 + FRAME));
  for (uint8_t k = 0; k < 4; k++) {
    const int8_t d = k < 2 ? BOX_DEFAULT : BOX_DEFAULT_SIZE;
    b[k] = f == NO_FRAME ? d : s8(m.romb(f + 4 + k));
  }
}

bool overlap(int16_t a0, int16_t a1, int16_t b0, int16_t b1) {
  return (b0 <= a0 && a0 <= b1) || (b0 <= a1 && a1 <= b1);
}

// one step of 1 towards t (0 when there)
int16_t nudge(int16_t c, int16_t t) { return c > t ? -1 : (c < t ? 1 : 0); }

}  // namespace

void init(Mem &m) {
  for (uint8_t i = 0; i < NACIDS; i++) { m.wb(acid(i) + IDX, i); }
}

// 0x17346: per entry of 0x23d4a[race] not yet live whose band lo or hi is entered from outside
// [lo, hi]: spawn its acid(s), a full array ends the walk; then remove acid whose range is left
void bands(Mem &m, int8_t a, int8_t b) {
  for (uint32_t e = m.roml(SPAWN_TABLE + 4 * m.rw(marble_physics::RACE)); m.romb(e) != END;
       e += SPAWN_ENTRY) {
    const uint32_t path = m.roml(e + 2);
    const int8_t lo = s8(m.romb(e)), hi = s8(m.romb(e + 1));
    if (taken(m, path) || (b != lo && b != hi) || (lo <= a && a <= hi)) { continue; }
    if (!spawn(m, e, path)) { break; }
  }

  for (uint8_t i = 0; i < NACIDS; i++) {
    const uint32_t s = acid(i);
    if (!m.rb(s + SLOT)) { continue; }
    const int16_t lo = m.sw(s + BAND_LO), hi = m.sw(s + BAND_HI);
    if ((a == lo && b < lo) || (a == hi && b > hi)) {
      m.wb(s + SLOT, 0);
      marble_drawlist::remove(m, s, TYPE_ACID);
    }
  }
}

void update_17230(Mem &m) {
  for (uint8_t i = 0; i < NACIDS; i++) { tick_1725a(m, acid(i)); }
}

// 0x175c8 (races 2/5): player a2 (scratch 0x400690 = its new position, box x/y +-3, no height
// test) against the acid frame boxes; the first hit dissolves the marble (sound 0x5e)
bool contact(Mem &m, uint32_t a2) {
  const uint16_t race = m.rw(marble_physics::RACE);
  if (race != 2 && race != 5) { return false; }

  const int16_t wx = m.sw(POS_X), wy = m.sw(POS_Y);
  for (uint8_t i = 0; i < NACIDS; i++) {
    const uint32_t s = acid(i);
    if (!m.rb(s + SLOT)) { continue; }
    int16_t b[4];
    box(m, s, b);
    const int16_t x0 = s16(m.rw(s + PX) + b[0]), y0 = s16(m.rw(s + PY) + b[1]);
    if (!overlap(s16(wx - BOX_XY), s16(wx + BOX_XY), x0, s16(x0 + b[2]))) { continue; }
    if (!overlap(s16(wy - BOX_XY), s16(wy + BOX_XY), y0, s16(y0 + b[3]))) { continue; }
    marble_physics::enterState(m, a2, marble_physics::DISSOLVING);
    marble_sound::send(m, marble_sound::SND_DISSOLVE);
    m.wb(a2 + P_ACID, i);
    m.wb(s + TICKS, HIT_TICKS);
    return true;
  }
  return false;
}

// 0x176d2: the dissolving marble slides 1 px per tick on x and y towards the centre of its
// acid's box; positions keep whole pixels
void target(Mem &m, uint32_t a2) {
  const uint32_t s = ACIDS + (uint32_t)(ACID_SIZE * s8(m.rb(a2 + P_ACID)));
  int16_t b[4];
  box(m, s, b);
  const int16_t tx = s16((uint32_t)((b[2] >> 1) + b[0] + m.rw(s + PX)));
  const int16_t ty = s16((uint32_t)((b[3] >> 1) + b[1] + m.rw(s + PY)));
  const int16_t x = m.sw(a2 + PX), y = m.sw(a2 + PY);
  m.wl(a2 + PX, (uint32_t)(uint16_t)(x + nudge(x, tx)) << 16);
  m.wl(a2 + PY, (uint32_t)(uint16_t)(y + nudge(y, ty)) << 16);
}

}  // namespace marble_acid

namespace marble_enemies {

using marble_physics::Mem;

void acidInit(Mem &m) { marble_acid::init(m); }
void acidBands(Mem &m, int8_t a, int8_t b) { marble_acid::bands(m, a, b); }
void acid_17230(Mem &m) { marble_acid::update_17230(m); }
bool contact_175c8(Mem &m, uint32_t a2) { return marble_acid::contact(m, a2); }
void target_176d2(Mem &m, uint32_t a2) { marble_acid::target(m, a2); }

}  // namespace marble_enemies

#endif  // GAME_MARBLE
