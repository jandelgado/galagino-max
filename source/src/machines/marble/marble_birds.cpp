#include "marble_birds.h"
#include "marble_enemies.h"
#include "marble_drawlist.h"
#include "marble_occlusion.h"
#include "marble_physics.h"
#include "marble_rng.h"

#ifdef ENABLE_MARBLE

// 1:1 port of tools/marble_bird.py (function names as there; Marble Love
// scroll-range-144e4.ts mode 4, state-sub-19baa.ts, state-sub-19a40.ts,
// marble-cell-dispatch-19e42.ts, bbox-hit-test-19d94.ts). Integer semantics follow the model.
//
// While the camera band is in 3-0x1b, birds spawn at 5 fixed cells every 8 logic ticks and
// walk down the course (y only); terrain rising above them makes them land, then they leave.
// A bird walking into the marble carries it off (player state CARRIED, then LOST).

// everything in its own namespace: the host tests compile every module into one unit, where
// same-named constants of the other enemy files would hide these
namespace marble_birds {

using marble_physics::Mem;
using marble_physics::s8;
using marble_physics::s16;
using marble_enemies::BIRDS;
using marble_enemies::BIRD_SIZE;
using marble_enemies::NBIRDS;

namespace {

// bird fields (MARBLE.md "Birds in the main tick model")
enum : uint8_t {
  VY = 0x04, PX = 0x0c, PY = 0x10, PZ = 0x14,
  SLOT = 0x18, IDX = 0x19, STATE = 0x1a, LOOPS = 0x1b,
  ANIM = 0x1c,                     // long: animation cursor
  SCREEN = 0x20,                   // sx, sy words
  TICK = 0x24, TICKS = 0x25,       // frame ticker, ticks per frame
  MASKS = 0x26,                    // 3 x {code, x, y}
};
enum : uint8_t { WALK = 0, STAND = 1, DONE = 2 };   // +0x1a; DONE: landed or carrying

// player fields
enum : uint8_t { P_STATE = 0x1a, P_DAMAGE = 0x57 };

static const uint8_t LIVE = 1, TYPE_BIRD = 0xf;     // draw list type
static const uint8_t SILLY = 4;
static const uint8_t NMASKS = 3;
static const int8_t BAND_LO = 3, BAND_HI = 0x1b;     // spawning band (0x144e4)
static const uint8_t SPAWN_EVERY = 7;               // 0x400010 & 7 == 0
static const uint8_t NPOINTS = 5, PASSES = 2;
static const int16_t ROWS_BELOW = 4;                // a lone bird in the column: 4+ cells down
static const uint8_t TICKS_SPAWN = 8, TICKS_FAST = 1, TICKS_SLOW = 4, TICKS_CARRY = 4;
static const uint8_t LOOPS_FAST = 4, LOOPS_SLOW = 1;  // + rng(2)
static const int16_t BOX_X = 6, BOX_Y = 4, BOX = 12;  // contact box x - 6, y - 4, 12 x 12

// mask cells (0x19e42): columns x 0x29/0x31/0x39 from row 0x34
static const uint8_t MASK_X0 = 0x29, MASK_X1 = 0x31, MASK_X2 = 0x39;
static const int8_t MASK_Y = 0x34;

// work RAM
static const uint32_t SPAWN_ON = 0x400762;           // byte, set by birdBands
static const uint32_t POS_X = 0x400690, POS_Y = 0x400692;  // words

// program ROM
static const uint32_t POINTS = 0x244f6;              // 5 spawn cells (x, y bytes)
static const uint32_t NO_FRAME = 0xffffffff;
static const uint32_t ANIM_SPAWN = 0x224ca, ANIM_WALK = 0x224d6;
static const uint32_t ANIM_LAND = 0x224e2, ANIM_CARRY = 0x22546;

static const uint32_t Z0 = 0x3fd80000;
static const uint32_t V_FAST = 0xfffc0000, V_SLOW = 0xfffe0000;  // -4, -2 px per tick
static const uint32_t GRID = 0xfffc0000;            // landing: old y back to the 4 px grid
static const int32_t CENTRE = 0x40000;

uint32_t bird(uint8_t i) { return BIRDS + BIRD_SIZE * i; }

bool inBand(int8_t b) { return BAND_LO <= b && b <= BAND_HI; }

// 0x19e42: screen position +0x20 as slot objects; masks 0x264aa mode 3 on the mask cells,
// else the 3 entries cleared
void screen_19e42(Mem &m, uint32_t a2) {
  const uint16_t x = m.rw(a2 + PX), y = m.rw(a2 + PY);
  m.ww(POS_X, x);
  m.ww(POS_Y, y);
  const uint16_t sx = (uint16_t)(y - x + 0x88);
  const uint16_t sy = (uint16_t)(m.rw(marble_physics::CAMERA) + m.rw(a2 + PZ) + 0x54 -
                                 (uint16_t)(((int32_t)s16(x) + s16(y)) >> 1));
  m.wl(a2 + SCREEN, (uint32_t)sx << 16 | sy);

  const uint8_t cx = (uint8_t)(s16(x) >> 3);
  const int8_t cy = s8((uint32_t)(s16(y) >> 3));
  if ((cx == MASK_X0 || cx == MASK_X1 || cx == MASK_X2) && cy >= MASK_Y) {
    marble_occlusion::bird(m, a2);
    return;
  }
  for (uint8_t i = 0; i < NMASKS; i++) { m.ww(a2 + MASKS + 6 * i, 0); }
}

// live birds standing in cell column cx; first: the lowest index of them
uint8_t inColumn(const Mem &m, int8_t cx, uint8_t &first) {
  uint8_t n = 0;
  for (uint8_t k = 0; k < NBIRDS; k++) {
    if (m.rb(bird(k) + SLOT) != LIVE || (uint16_t)(m.sw(bird(k) + PX) >> 3) != (uint16_t)cx) {
      continue;
    }
    if (!n) { first = k; }
    n++;
  }
  return n;
}

// lowest slot not live, 0 if none
uint32_t freeSlot(const Mem &m) {
  for (uint8_t k = 0; k < NBIRDS; k++) {
    if (m.rb(bird(k) + SLOT) != LIVE) { return bird(k); }
  }
  return 0;
}

void init_19a40(Mem &m, uint32_t a, int8_t cx, int8_t cy) {
  m.wb(a + SLOT, LIVE);
  m.wb(a + STATE, STAND);
  m.wl(a + PX, (uint32_t)(cx * 0x80000 + CENTRE));
  m.wl(a + PY, (uint32_t)(cy * 0x80000 + CENTRE));
  m.wl(a + PZ, Z0);
  m.wb(a + TICK, 0);
  m.wb(a + LOOPS, 0);
  m.wb(a + TICKS, TICKS_SPAWN);
  m.wl(a + ANIM, ANIM_SPAWN);
  m.wl(a + VY, V_FAST);
  screen_19e42(m, a);
  marble_drawlist::insert(m, a, TYPE_BIRD);
}

// 0x19a40: two passes over the 5 spawn cells, at most one spawn per pass: a cell spawns when
// at most pass-many live birds stand in its column (one: only 4+ cells below the spawn row);
// lowest free slot, none free ends the scan
void spawn_19a40(Mem &m) {
  for (uint8_t pass = 0; pass < PASSES; pass++) {
    for (uint8_t k = 0; k < NPOINTS; k++) {
      const int8_t cx = s8(m.romb(POINTS + 2 * k)), cy = s8(m.romb(POINTS + 2 * k + 1));
      uint8_t first = 0;
      const uint8_t n = inColumn(m, cx, first);
      if (n > pass) { continue; }
      if (n == 1 && ROWS_BELOW > cy - (m.sw(bird(first) + PY) >> 3)) { continue; }

      const uint32_t a = freeSlot(m);
      if (!a) { return; }
      init_19a40(m, a, cx, cy);
      break;
    }
  }
}

// another live bird in a2's column further up (smaller y)
bool ahead(const Mem &m, uint32_t a2) {
  for (uint8_t k = 0; k < NBIRDS; k++) {
    const uint32_t o = bird(k);
    if (o != a2 && m.rb(o + SLOT) == LIVE && m.rw(o + PX) == m.rw(a2 + PX) &&
        m.sw(a2 + PY) > m.sw(o + PY)) {
      return true;
    }
  }
  return false;
}

// animation list end of a bird not DONE: walk again; after its loops a new speed: fast if it
// walked slow and no bird is ahead in its column, else slow
void rewalk(Mem &m, uint32_t a2) {
  const bool blocked = ahead(m, a2);
  m.wl(a2 + ANIM, ANIM_WALK);
  m.wb(a2 + STATE, WALK);
  if (m.rb(a2 + LOOPS)) { m.wb(a2 + LOOPS, m.rb(a2 + LOOPS) - 1); }
  if (m.rb(a2 + LOOPS)) { return; }

  if (m.rl(a2 + VY) == V_SLOW && !blocked) {
    m.wb(a2 + TICKS, TICKS_FAST);
    m.wl(a2 + VY, V_FAST);
    m.wb(a2 + LOOPS, (uint8_t)(marble_rng::rng(m, 2) + LOOPS_FAST));
    return;
  }
  m.wb(a2 + TICKS, TICKS_SLOW);
  m.wl(a2 + VY, V_SLOW);
  m.wb(a2 + LOOPS, (uint8_t)(marble_rng::rng(m, 2) + LOOPS_SLOW));
}

// walking: y += v; terrain above the bird's z: back to the grid of the old y, landing anim
void walk(Mem &m, uint32_t a2) {
  const uint32_t y0 = m.rl(a2 + PY);
  m.wl(a2 + PY, y0 + m.rl(a2 + VY));
  if (marble_physics::cellHeight(m, a2) <= m.sl(a2 + PZ)) { return; }

  m.wl(a2 + PY, y0 & GRID);
  m.wl(a2 + ANIM, ANIM_LAND);
  m.wb(a2 + TICK, 0);
  m.wb(a2 + TICKS, TICKS_FAST);
  m.wb(a2 + STATE, DONE);
}

// 0x19baa per bird: frame ticker (list end: walk again, or removal when DONE), walk, screen
void tick(Mem &m, uint32_t a2) {
  if (!m.rb(a2 + SLOT)) { return; }

  m.wb(a2 + TICK, m.rb(a2 + TICK) + 1);
  if (m.sb(a2 + TICKS) <= m.sb(a2 + TICK)) {
    m.wb(a2 + TICK, 0);
    m.wl(a2 + ANIM, m.rl(a2 + ANIM) + 4);
    const bool end = m.roml(m.rl(a2 + ANIM)) == NO_FRAME;
    if (end && m.rb(a2 + STATE) != DONE) {
      rewalk(m, a2);
    } else if (end) {
      m.wb(a2 + SLOT, 0);
      marble_drawlist::remove(m, a2, TYPE_BIRD);
    }
  }

  if (m.rb(a2 + STATE) == WALK) { walk(m, a2); }
  screen_19e42(m, a2);
}

}  // namespace

void init(Mem &m) {
  for (uint8_t i = 0; i < NBIRDS; i++) { m.wb(bird(i) + IDX, i); }
}

// 0x144e4 mode 4 (silly race), after the slot spawner: spawning on while the band is in
// 3-0x1b (signed band bytes)
void bands(Mem &m, int8_t a, int8_t b) {
  if (m.rw(marble_physics::RACE) != SILLY) { return; }
  if (!inBand(a) && inBand(b)) {
    m.wb(SPAWN_ON, 1);
  } else if (inBand(a) && !inBand(b)) {
    m.wb(SPAWN_ON, 0);
  }
}

// 0x19baa (silly race): spawn every 8 logic ticks while on, then every bird
void update_19baa(Mem &m) {
  if (m.rw(marble_physics::RACE) != SILLY) { return; }
  if (m.rb(SPAWN_ON) && !(m.rl(marble_physics::FRAMES) & SPAWN_EVERY)) { spawn_19a40(m); }
  for (uint8_t i = 0; i < NBIRDS; i++) { tick(m, bird(i)); }
}

// 0x19d94 (silly race): player a2 (scratch 0x400690 = its new position, no height test)
// against every walking bird's 12 x 12 box: the bird carries it (sound 0x3e)
void contact(Mem &m, uint32_t a2) {
  if (m.rw(marble_physics::RACE) != SILLY) { return; }

  const int16_t mx = m.sw(POS_X), my = m.sw(POS_Y);
  for (uint8_t i = 0; i < NBIRDS; i++) {
    const uint32_t s = bird(i);
    if (!m.rb(s + SLOT) || m.rb(s + STATE) != WALK) { continue; }
    const int16_t left = s16(m.rw(s + PX) - BOX_X), top = s16(m.rw(s + PY) - BOX_Y);
    if (left > mx || s16(left + BOX) <= mx || top > my || s16(top + BOX) <= my) { continue; }

    m.wb(s + STATE, DONE);
    m.wl(s + ANIM, ANIM_CARRY);
    m.wb(s + TICKS, TICKS_CARRY);
    m.wb(s + TICK, 0);
    m.wb(a2 + P_STATE, marble_physics::CARRIED);
    m.wb(a2 + P_DAMAGE, marble_physics::CARRIED_DAMAGE);
  }
}

}  // namespace marble_birds

namespace marble_enemies {

using marble_physics::Mem;

void birdInit(Mem &m) { marble_birds::init(m); }
void birdBands(Mem &m, int8_t a, int8_t b) { marble_birds::bands(m, a, b); }
void birds_19baa(Mem &m) { marble_birds::update_19baa(m); }
void contact_19d94(Mem &m, uint32_t a2) { marble_birds::contact(m, a2); }

}  // namespace marble_enemies

#endif  // ENABLE_MARBLE
