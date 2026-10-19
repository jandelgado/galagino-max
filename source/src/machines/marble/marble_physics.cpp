#include "marble_physics.h"
#include "marble_drawlist.h"
#include "marble_enemies.h"
#include "marble_flow.h"
#include "marble_objects.h"
#include "marble_occlusion.h"
#include "marble_rng.h"
#include "marble_sound.h"

#ifdef ENABLE_MARBLE

// 1:1 port of tools/marble_sim.py (function names and order as there). Integer
// semantics of the 68010: word results wrap at 16 bits, long results at 32 bits (done in
// uint32_t), >> on signed values is asr, int64_t where the model's values exceed 32 bits.

namespace marble_physics {

namespace {

// object fields (MARBLE.md "Marble physics")
enum : uint8_t {
  VX = 0x00, VY = 0x04, VZ = 0x08,  // velocity, 16.16 per tick
  PX = 0x0c, PY = 0x10, PZ = 0x14,  // position, 16.16
  SLOT = 0x18,                      // 1 in play, 2/3 out of play
  PLAYER_NO = 0x19,
  STATE = 0x1a,                     // enum State
  ZONE = 0x1b, VISIBLE = 0x1c, GROUND_ZONE = 0x1d,
  SCREEN = 0x1e, SCREEN_OLD = 0x22, SCREEN_OLD2 = 0x26,  // sx, sy words
  GROUND_Z = 0x2a,                  // z at last ground (fall height)
  EDGE_CX = 0x2e, EDGE_CY = 0x30,   // cell where teetering / the fall started
  ORBIT_ANGLE = 0x2e,               // state 4: sparkle angle
  GROUND_CX = 0x32, GROUND_CY = 0x34,
  AIR = 0x36,                       // 0 ground, 1 teetering, 2 airborne
  EDGE_MASK = 0x37,
  MASKS = 0x38,                     // 5 x {code, x, y}: masks, state 4 sparkle stars
  DIZZY_CNT = 0x56, DAMAGE = 0x57,  // damage: countdown in states 4 and 6
  SURFACE = 0x58, SURFACE_T = 0x59,
  ANIM = 0x5a, ANIM_5F = 0x5f, ANIM_60 = 0x60,  // frame list, tick counter, ticks per frame
  SHARDS = 0x62, SHARD_TICK = 0x66, SHARDS_ON = 0x67,  // shatter overlay list
  STARS = 0xa4,                     // 4 x {code, x, y}: dots, state 4 sparkle stars
  TIMER = 0x6a, TIMER_KEPT = 0x6d, TIMER_TICKS = 0x6e,  // race timer (marble_flow.h)
  SCORE = 0xbc, DISTANCE = 0xc4,
  BALL_C6 = 0xc6, BALL_C7 = 0xc7,   // trackball deltas port 2 / port 0
  BONUS_EVENT = 0xcb,               // 0x1bbaa: time bonus pending
  BONUS_ANIM = 0xcc, BONUS_TICK = 0xd0, BONUS_ON = 0xd1,
  DEATHS = 0xd2,
};

enum : uint8_t { ON_GROUND = 0, TEETER = 1, AIRBORNE = 2 };

// work RAM scratch of 0x121b8 and its helpers
static const uint32_t HIT_X = 0x400666, HIT_Y = 0x400668;  // wall bounce flags
static const uint32_t DIAG_MASK = 0x40066a;                // diagonal wall flags (0x1c676)
static const uint32_t EDGE_PX = 0x40066c, EDGE_PY = 0x40066e;  // side edge codes 1-4
static const uint32_t EDGE_NX = 0x400670, EDGE_NY = 0x400672;
static const uint32_t WALL_PX = 0x400674, WALL_PY = 0x400676;  // side wall heights - z
static const uint32_t WALL_NX = 0x400678, WALL_NY = 0x40067a;
static const uint32_t WALL_D0 = 0x40067c, WALL_D1 = 0x40067e;  // diagonal wall heights - z
static const uint32_t WALL_D2 = 0x400680, WALL_D3 = 0x400682;
static const uint32_t OLD_X = 0x400684, OLD_Y = 0x400688, OLD_Z = 0x40068c;  // longs
static const uint32_t POS_X = 0x400690, POS_Y = 0x400692, POS_Z = 0x400694;  // words
static const uint32_t CELL_X = 0x400696, CELL_Y = 0x400698;
static const uint32_t OLD_CX = 0x40069a, OLD_CY = 0x40069c;
static const uint32_t FRAC_X = 0x40069e, FRAC_Y = 0x4006a0;  // position in the cell 0-7
static const uint32_t SPLIT = 0x4006a2;                      // triangle: fy >= fx
static const uint32_t GRAD_X = 0x4006a4, GRAD_Y = 0x4006a6;
static const uint32_t GAME_STATE = 0x400390;                 // word, marble_flow::Phase
static const uint32_t GOAL_FIRST = 0x4003a4;                 // 0xff: nobody at the goal yet
static const uint32_t SPAWN_X = 0x400462, SPAWN_Y = 0x400466;  // longs, respawn pixel
static const uint32_t SPAWN_ZONE = 0x400472;                 // 0xff: none found
static const uint32_t VERTS = 0x401c28;                      // 4 vertices x 4 heights
static const uint32_t PLAYERS = 0x400396;                    // word: player count
static const uint32_t SCORED = 0x40039c;                     // bit per player: score changed
static const uint32_t TIMEOUT_RND = 0x400444;                // race start random byte
static const uint32_t BLOCK_HIT = 0x40046a;                  // long: index 0x17cb8 found
static const uint32_t TIMEOUT_ARMED = 0x40076c;              // time bonus event 0x1bbaa armed
static const uint32_t ZONE_SOUNDS = 0x40076a;                // zone hook sound latches
static const uint32_t MODE3_TIMER = 0x400768;                // word: aerial sound countdown
static const uint32_t STEELIES = 0x4009a4, SLINKIES = 0x401302;

// program ROM tables
static const uint32_t DYN_INTERMEDIATE = 0x1d370;  // 66 dynamic codes (0x11ac2)
// first animation frame of the dynamic codes (0x13628): word slot, word count, words
static const uint32_t DYN_PRACTICE = 0x1699c, DYN_BEGINNER = 0x1b908, DYN_ULTIMATE = 0x24e4e;
static const uint32_t ZONE_LISTS = 0x1eb16;        // zone line list per race
static const uint32_t GOAL_VX = 0x1eb2e, GOAL_VY = 0x1eb34;
static const uint32_t CHECKPOINTS = 0x1ef1a;       // list per race: cx, cy, zone, pad; 0xff end
static const uint32_t DROP = 0x1ed62;              // two level code height steps
static const uint32_t FRICTION = 0x1eef8;
static const uint32_t TEETER_LIFT = 0x2398c;   // z lift by distance to the edge
static const uint32_t PUSH = 0x23d40;
static const uint32_t STEPS = 0x24b3a;             // vertex walk of 0x1caba
// animation lists of +0x5a (long frame headers, LIST_END), shatter overlay +0x62
static const uint32_t ANIM_DIZZY = 0x20faa, ANIM_DIZZY_LOOP = 0x20fb6, ANIM_DIZZY_END = 0x20fd2;
static const uint32_t ANIM_SHATTER = 0x20fde, ANIM_SHARDS = 0x215f6;
static const uint32_t ANIM_DISSOLVE = 0x21062;     // state 9 (acid)
static const uint32_t ANIM_BONUS = 0x216aa;        // state 8 +0xcc
static const uint32_t BONUS_LIMIT = 0x24a94;       // per race: event when the rng byte is below
static const uint32_t BONUS_ZONES = 0x24aae;       // per race: zone list (0xff end)
static const uint32_t SINE = 0x1eda2;              // 0x4000 = 1, quarter turn QUARTER entries
static const uint32_t STAR_STEPS = 0x1ef32;        // 8 x (s8 dx, s8 dy)

static const uint32_t LIST_END = 0xffffffff;
static const uint32_t SHATTER_BYTES = 0x80;        // 32 entries
static const uint16_t SHATTER_SHARDS = 9;          // shatter frame that starts the overlay
static const uint8_t ANIM_RATE = 2;                // ticks per frame
static const uint8_t BONUS_RATE = 9, BONUS_SECONDS = 10;     // time bonus: ticks per second
static const uint8_t BONUS_SURFACE_A = 0x35, BONUS_SURFACE_B = 0x34;  // besides 0
static const uint8_t SCORE_BONUS_END = 0x10;                  // 0x285b0 entry
static const uint8_t DISSOLVE_RATE = 4;            // ticks per dissolve frame
static const uint8_t DIZZY_LOOPS = 6;              // +0x56 above: loop list, else end list
static const uint8_t OUT_OF_LIST = 2, OUT_OF_PLAY = 3;  // +0x18: 2 also off the draw list
static const uint8_t SHATTER_CNT = 2;              // +0x56 in state 2 (0, 1: other lists)
static const int8_t SPARKLE_LAST = 2;              // +0x57 below: last sparkle tick
static const uint8_t FELL = 0x64, EMBEDDED = 0x65;  // +0x57 at state 4 entry (CARRIED_DAMAGE)
static const uint8_t BREAK_LIMIT = 0x1f;           // +0x56 + (+0x57 - +0x56) / 2 above: break
static const int16_t BREAK_SY_MIN = -0x10, BREAK_SY_MAX = 0xf0;  // on screen: shatter
// sparkle 0x13ade: ticks by +0x57, angle steps of a full turn, stars on screen
static const uint8_t ORBIT_FELL = 0x30, ORBIT_EMBEDDED = 0x18, ORBIT_CARRIED = 0x24;
static const int16_t FULL_TURN = 0x192, QUARTER = 0x64, ORBIT_STEP = 10, STAR_STEP = 0x32;
static const uint16_t STAR_CODE = 0x10b;           // MO code of star k: + k, no mask bit
static const uint8_t STAR_N = 8, STAR_SLOTS = 4;   // 4 at +0xa4, then 4 at +0x38
static const int16_t STAR_MIN = -8, STAR_X_MAX = 0x120, STAR_Y_MAX = 0xf0;
// 0x17cb8: objects near a pixel, radius of the octant distance (pixels * 16)
static const uint16_t SPAWN_FREE = 0x180, RESPAWN_FREE = 0x70;
static const uint8_t NEAR_CELLS = 3;               // both deltas below 32 px
static const uint16_t PLAYER_SIZE = 0xe2, STEELIE_SIZE = 0x7c, SLINKY_SIZE = 0x60;
static const uint8_t STEELIE_N = 2, SLINKY_N = 4;
static const uint8_t HIT_PLAYER = 1, HIT_STEELIE = 2, HIT_SLINKY = 4;
static const int16_t SCORE_STEP = 0xa0, SCORE_POINTS = 10;  // distance score 0x26214
static const uint32_t SCORES = 0x23cd4, SCORE_TEXTS = 0x23cf6;  // 0x285b0: words, longs
static const uint8_t SCORE_SHOWN = 0x68, SCORE_FLAG = 0x69, SCORE_TEXT_ON = 0x70;  // +0x68..
static const uint8_t SCORE_TEXT = 0xd4, SCORE_WAIT = 0xd8;
static const uint8_t PULSE_FLASH = 0x71;  // timer flash counter (marble_flow)
static const int8_t WOBBLE_HI = 2, WOBBLE_LO = -0x16, WOBBLE_STEPS = 0x28;  // 0x25416
static const uint16_t RNG_BYTE = 0x100;

static const uint32_t FALL_VZ = 0xffffa000;        // -0x6000: vz when a fall starts
static const uint8_t RACE_PRACTICE = 0, RACE_BEGINNER = 1, RACE_INTERMEDIATE = 2;
static const uint8_t RACE_AERIAL = 3, RACE_SILLY = 4, RACE_ULTIMATE = 5;
static const uint8_t ZONE_SLOTS = 4;  // aerial zone whose entry resets its slots (0x13892)
static const uint8_t DYN_INTERMEDIATE_N = 0x42;
static const uint16_t SPAWN_RANGE = 0x300;         // max checkpoint distance (max + 3/8 min) * 16
static const uint8_t NO_ZONE = 0xff;
static const uint8_t TYPE_STEELIE = 2;   // draw list type

inline int32_t s32(int64_t v) { return (int32_t)(uint32_t)v; }
inline int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t shl32(int32_t v, uint8_t n) { return (int32_t)((uint32_t)v << n); }
inline int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

uint32_t divu(uint32_t n, uint32_t d) {
  if (d == 0) { fail("divide by zero", n); }
  return n / d;
}

// max + 3/8 min of |vx|, |vy| (0x261bc, 0x25e7c)
uint32_t speedNorm(int32_t vx, int32_t vy) {
  const uint32_t a = (uint32_t)abs64(vx), b = (uint32_t)abs64(vy);
  if (a > b) { return ((b >> 3) & 0xffff) * 3 + a; }
  return ((a >> 3) & 0xffff) * 3 + b;
}

// v = (v >> 8) * k >> 6 (muls)
uint32_t scaleV(int32_t v, uint16_t k) { return (uint32_t)((s16(v >> 8) * s16(k)) >> 6); }

void terrain_1caba(Mem &m);
void enter_25bae(Mem &m, uint32_t a2, State code);
void proj_1b9cc(Mem &m, uint32_t a2, Refetch r);

// 0x1bb50: derive cell / fraction fields from 0x400690/0x400692
void derive_1bb50(Mem &m) {
  const uint16_t x = m.rw(POS_X), y = m.rw(POS_Y);
  m.ww(FRAC_X, x & 7);
  m.ww(FRAC_Y, y & 7);
  m.ww(CELL_X, (uint16_t)(s16(x) >> 3));
  m.ww(CELL_Y, (uint16_t)(s16(y) >> 3));
  m.ww(SPLIT, (y & 7) >= (x & 7) ? 1 : 0);
}

// 0x1bab2: copy object x/y/z words to 0x400690.. and refetch terrain on cell change
void posupd_1bab2(Mem &m, uint32_t a) {
  const uint16_t ox = m.rw(CELL_X), oy = m.rw(CELL_Y);
  m.ww(POS_X, m.rw(a + PX));
  m.ww(POS_Y, m.rw(a + PY));
  m.ww(POS_Z, m.rw(a + PZ));
  derive_1bb50(m);
  if (m.rw(CELL_X) != ox || m.rw(CELL_Y) != oy) { terrain_1caba(m); }
}

// 0x1cb64: terrain code -> 4 vertex heights, one per quadrant. 0 = no floor.
void codeHeights(const Mem &m, uint16_t code, uint16_t base, uint16_t h[4]) {
  // dynamic codes 0x0800-0x0fff: RAM table 0x40076e (moving parts)
  while (code >= 0x800 && code < 0x1000) { code = m.rw(DYN + (code & 0x7fe)); }

  // flat: base + (code & 0x7f) - 0x40
  if (code >= 0xf000) {
    const uint16_t v = (code & 0x7f) - 0x40 + base;
    h[0] = h[1] = h[2] = h[3] = v;
    return;
  }

  // two levels, bits 12-15 pick the high one
  if (code >= 0x1000) {
    const uint16_t v0 = (code & 0x7f) - 0x40 + base;
    const uint16_t coef = m.romw(DROP + ((s16(code) >> 6) & 0x3e));
    const uint16_t alt = coef == 0x1000 ? 0 : (uint16_t)(s16(v0) - s16(coef));
    for (uint8_t q = 0; q < 4; q++) { h[q] = (code & (1 << (12 + q))) ? v0 : alt; }
    return;
  }

  if (code == 0) {
    h[0] = h[1] = h[2] = h[3] = 0;
    return;
  }

  // 4 byte record at descriptor +0x00
  const uint16_t d1 = base - 0x80;
  for (uint8_t q = 0; q < 4; q++) {
    const uint8_t b = m.record(code, q);
    h[q] = b == 0 ? 0 : (uint16_t)(b + d1);
  }
}

// 0x1caba: fetch the 4 vertex records around the cell (0x400696, 0x400698) into 0x401c28.
// Diagonal d = cx + cy + 1 - 0x15, column c = 0x15 - cx + d / 2; steps from table 0x24b3a.
void terrain_1caba(Mem &m) {
  uint32_t a5 = VERTS;
  const uint16_t tx = m.rw(CELL_X);
  uint16_t d4 = m.rw(CELL_Y) + 1 + tx - 0x15;
  uint16_t d6 = 0x15 - tx + (s16(d4) >> 1);
  uint32_t a6 = STEPS + ((d4 & 1) ? 0x12 : 0);

  for (uint8_t i = 0; i < 4; i++) {
    const int16_t d = s16(d4);
    uint16_t h[4] = {0, 0, 0, 0};
    if (d >= 0 && d < m.maxd()) {
      const int16_t f = s16(d6) + ((d & 1) ? 0x16 : 0);
      codeHeights(m, m.vertexCode(d, f), m.base(d), h);
    }
    for (uint8_t k = 0; k < 4; k++) { m.ww(a5 + 2 * k, h[k]); }
    a5 += 8;

    const uint16_t st1 = m.romw(a6), st2 = m.romw(a6 + 2), st3 = m.romw(a6 + 4);
    a6 += 6;
    d6 += st1;
    d4 += st2;
    a5 += s16(st3);
  }
}

// 0x1cc62: height under the marble (16.16), gradient to 0x4006a4/0x4006a6
int32_t height_1cc62(Mem &m) {
  const uint16_t cx0 = m.rw(VERTS + 4), cx1 = m.rw(VERTS + 0xe);
  const uint16_t cy0 = m.rw(VERTS + 0x10), cz = m.rw(VERTS + 0x1a);
  uint16_t dx, dy;
  if (m.rw(SPLIT)) {
    dx = cx1 - cx0;
    dy = cx0 - cz;
  } else {
    dx = cy0 - cz;
    dy = cx1 - cy0;
  }
  m.ww(GRAD_X, dx);
  m.ww(GRAD_Y, dy);

  const int32_t d2 = shl32(s16(cz), 16);
  const int32_t d1 = s32((int64_t)s16(dy) * s16(m.rw(FRAC_Y)) + (int64_t)s16(dx) * s16(m.rw(FRAC_X)));
  return add32(d2, shl32(d1, 13));
}

// one side of 0x1c676: wall height (out) and edge code 1-4 (dir)
void edgeSide(Mem &m, uint32_t out, uint32_t dir, uint16_t key, uint16_t hi, uint16_t t1,
              uint16_t t2, uint16_t lo, uint16_t hi2, uint16_t probe, uint16_t dlo,
              uint16_t pivot, uint16_t l1h, uint16_t l1l, uint16_t l3h, uint16_t l3l,
              uint16_t f) {
  if (key == hi && t1 == t2) { return; }  // quirk: out keeps its stale value

  uint8_t d;
  if (s16(key) < s16(hi) || s16(probe) < s16(dlo)) {
    d = 1;
    m.ww(out, lo);
  } else {
    d = 3;
    m.ww(out, hi2);
  }
  if (pivot == hi) { d++; }
  m.wb(dir, d);

  if (d != 1 && d != 3) { return; }
  const int16_t diff = d == 1 ? s16(s16(l1h) - s16(l1l)) : s16(s16(l3h) - s16(l3l));
  m.ww(out, m.rw(out) + ((diff * s16(f) + 4) >> 3));
}

// vertex quadrant k is above all others
bool topQuadrant(const uint16_t *rec, uint8_t k, const uint8_t *others) {
  for (uint8_t i = 0; i < 3; i++) {
    if (!(s16(rec[others[i]]) < s16(rec[k]))) { return false; }
  }
  return true;
}

// 0x1c676: edge/wall heights around the cell
void edges_1c676(Mem &m) {
  m.wb(DIAG_MASK, 0);
  m.wb(EDGE_NY, 0);
  m.wb(EDGE_NX, 0);
  m.wb(EDGE_PY, 0);
  m.wb(EDGE_PX, 0);

  uint16_t s[4][4];
  for (uint8_t i = 0; i < 4; i++) {
    for (uint8_t k = 0; k < 4; k++) { s[i][k] = m.rw(VERTS + 8 * i + 2 * k); }
  }
  const uint16_t *s1 = s[0], *s2 = s[1], *s3 = s[2], *s4 = s[3];
  const uint16_t fa = m.rw(FRAC_Y), fb = m.rw(FRAC_X);

  edgeSide(m, WALL_PX, EDGE_PX, s1[2], s1[3], s4[1], s4[0], s4[0], s4[1], s4[1], s4[0], s4[0],
           s1[3], s4[0], s1[2], s4[1], fa);
  edgeSide(m, WALL_PY, EDGE_PY, s1[2], s1[1], s2[3], s2[0], s1[1], s1[2], s2[3], s2[0], s2[0],
           s2[0], s1[1], s2[3], s1[2], fb);
  edgeSide(m, WALL_NX, EDGE_NX, s3[0], s3[1], s2[3], s2[2], s2[2], s2[3], s2[3], s2[2], s2[2],
           s2[2], s3[1], s2[3], s3[0], fa);
  edgeSide(m, WALL_NY, EDGE_NY, s3[0], s3[3], s4[1], s4[2], s3[3], s3[0], s4[1], s4[2], s4[2],
           s3[3], s4[2], s3[0], s4[1], fb);

  // diagonal walls: one quadrant of a vertex above the other three
  struct Check { uint8_t bit, rec, k, others[3]; uint32_t out; };
  static const Check checks[8] = {
      {0x01, 0, 0, {2, 3, 1}, WALL_D0}, {0x02, 1, 1, {3, 0, 2}, WALL_D1},
      {0x04, 2, 2, {0, 1, 3}, WALL_D2}, {0x08, 3, 3, {1, 2, 0}, WALL_D3},
      {0x10, 0, 2, {0, 3, 1}, WALL_D0}, {0x20, 1, 3, {1, 0, 2}, WALL_D1},
      {0x40, 2, 0, {2, 1, 3}, WALL_D2}, {0x80, 3, 1, {3, 2, 0}, WALL_D3},
  };
  uint8_t flags = 0;
  for (const Check &c : checks) {
    if (!topQuadrant(s[c.rec], c.k, c.others)) { continue; }
    flags |= c.bit;
    m.ww(c.out, s[c.rec][c.k]);
  }
  m.wb(DIAG_MASK, flags);

  // heights relative to z, stale outputs included
  const uint16_t base = m.rw(POS_Z);
  for (uint32_t a = WALL_PX; a <= WALL_D3; a += 2) { m.ww(a, m.rw(a) - base); }
}

inline int64_t absV(int64_t v) { return v < 0 ? -v : v; }
inline int64_t negV(int64_t v) { return v > 0 ? -v : v; }

// 0x1b5c2: wall bounce
void walls_1b5c2(Mem &m, uint32_t a2) {
  const int32_t d3i = m.sl(a2 + VX), d4i = m.sl(a2 + VY);
  int64_t d3 = d3i, d4 = d4i;
  const int16_t d5 = s16(m.rw(OLD_CX) - m.rw(CELL_X));
  const int16_t d6 = s16(m.rw(OLD_CY) - m.rw(CELL_Y));
  const int16_t wd2 = m.sw(FRAC_X), wa0 = m.sw(FRAC_Y);
  const int16_t gPX = m.sw(WALL_PX), gPY = m.sw(WALL_PY), gNX = m.sw(WALL_NX);
  const int16_t gNY = m.sw(WALL_NY), gD0 = m.sw(WALL_D0), gD1 = m.sw(WALL_D1);
  const int16_t gD2 = m.sw(WALL_D2), gD3 = m.sw(WALL_D3);
  const int8_t fPX = m.sb(EDGE_PX), fPY = m.sb(EDGE_PY), fNX = m.sb(EDGE_NX), fNY = m.sb(EDGE_NY);
  const uint8_t bm = m.rb(DIAG_MASK);

  if ((fPX != 0 && fPX < 3 && wd2 < 4 && gPX >= 4) || (fNX > 2 && d5 == 1 && gNX >= 4)) {
    d3 = absV(d3);
  }
  if ((fPY != 0 && fPY < 3 && wa0 > 4 && gPY >= 4) || (fNY > 2 && d6 == -1 && gNY >= 4)) {
    d4 = negV(d4);
  }
  if ((fNX != 0 && fNX < 3 && wd2 > 4 && gNX >= 4) || (fPX > 2 && d5 == -1 && gPX >= 4)) {
    d3 = negV(d3);
  }
  if ((fNY != 0 && fNY < 3 && wa0 < 4 && gNY >= 4) || (fPY > 2 && d6 == 1 && gPY >= 4)) {
    d4 = absV(d4);
  }
  if (((bm & 1) && d5 != -1 && d6 != 1 && wd2 < 4 && wa0 > 4 && gD0 >= 4) ||
      ((bm & 0x40) && d5 != 0 && d6 != 0 && gD2 >= 4)) {
    d4 = negV(d4);
    d3 = absV(d3);
  }
  if (((bm & 2) && d5 != 1 && d6 != 1 && wd2 > 4 && wa0 > 4 && gD1 >= 4) ||
      ((bm & 0x80) && d5 != 0 && d6 != 0 && gD3 >= 4)) {
    d4 = negV(d4);
    d3 = negV(d3);
  }
  if (((bm & 4) && d5 != 1 && d6 != -1 && wd2 > 4 && wa0 < 4 && gD2 >= 4) ||
      ((bm & 0x10) && d5 != 0 && d6 != 0 && gD0 >= 4)) {
    d4 = absV(d4);
    d3 = negV(d3);
  }
  if (((bm & 8) && d5 != -1 && d6 != -1 && wd2 < 4 && wa0 < 4 && gD3 >= 4) ||
      ((bm & 0x20) && d5 != 0 && d6 != 0 && gD1 >= 4)) {
    d4 = absV(d4);
    d3 = absV(d3);
  }

  // bounce: restore the axis position (sound 0x42)
  if (s32(d3) != d3i) {
    marble_sound::send(m, marble_sound::SND_BOUNCE);
    m.wb(HIT_X, 1);
    m.wl(a2 + PX, m.rl(OLD_X));
    m.wl(a2 + VX, (uint32_t)d3);
  }
  if (s32(d4) != d4i) {
    m.wb(HIT_Y, 1);
    m.wl(a2 + PY, m.rl(OLD_Y));
    m.wl(a2 + VY, (uint32_t)d4);
  }
}

// surface codes where 0x160f6 lets the marble teeter
bool teeterSurface(uint8_t c) {
  static const uint8_t codes[] = {0x00, 0x10, 0x12, 0x17, 0x18, 0x20, 0x2d, 0x2e, 0x2f, 0x30, 0x31,
                                  0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b};
  for (uint8_t v : codes) {
    if (v == c) { return true; }
  }
  return false;
}

// wall output below the edge: 0 <= v < 4
inline bool edgeOk(int16_t v) { return !(4 <= v) && !(-1 >= v); }

void startFall(Mem &m, uint32_t a2) {
  m.wb(a2 + AIR, AIRBORNE);
  m.ww(a2 + EDGE_CX, m.rw(CELL_X));
  m.ww(a2 + EDGE_CY, m.rw(CELL_Y));
  m.wl(a2 + VZ, FALL_VZ);
  marble_sound::send(m, marble_sound::SND_FALL);
}

// 0x160f6: edge teetering / leaving the ground
void edge_160f6(Mem &m, uint32_t a2, int32_t h) {
  uint16_t D2 = 0;
  const int16_t D3 = s16(m.rw(OLD_CX) - m.rw(CELL_X));
  const int16_t D4 = s16(m.rw(OLD_CY) - m.rw(CELL_Y));
  const int16_t tx = m.sw(FRAC_X), ty = m.sw(FRAC_Y);

  uint8_t i = m.rb(EDGE_PX);
  if (i != 0 && i < 3 && tx < 4 && D3 != 1 && edgeOk(m.sw(WALL_PX))) { D2 |= 1; }
  i = m.rb(EDGE_PY);
  if (i != 0 && i < 3 && ty > 4 && D4 != -1 && edgeOk(m.sw(WALL_PY))) { D2 |= 2; }
  i = m.rb(EDGE_NX);
  if (i != 0 && i < 3 && tx > 4 && D3 != -1 && edgeOk(m.sw(WALL_NX))) { D2 |= 4; }
  i = m.rb(EDGE_NY);
  if (i != 0 && i < 3 && ty < 4 && D4 != 1 && edgeOk(m.sw(WALL_NY))) { D2 |= 8; }

  const uint8_t dg = m.rb(DIAG_MASK);
  if ((dg & 1) && tx < 4 && ty > 4 && D3 == -1 && D4 == 1 && m.sw(WALL_D0) < 4) { D2 |= 0x10; }
  if ((dg & 2) && tx > 4 && ty > 4 && D3 == 1 && D4 == 1 && m.sw(WALL_D1) < 4) { D2 |= 0x20; }
  if ((dg & 4) && tx > 4 && ty < 4 && D3 == 1 && D4 == -1 && m.sw(WALL_D2) < 4) { D2 |= 0x40; }
  if ((dg & 8) && tx < 4 && ty < 4 && D3 == -1 && D4 == -1 && m.sw(WALL_D3) < 4) { D2 |= 0x80; }

  // start or keep teetering, or start a fall more than 6 px above the ground
  const uint8_t cc = m.rb(a2 + SURFACE);
  if (D2 && teeterSurface(cc)) {
    if (m.rb(a2 + AIR) == ON_GROUND) { m.wb(a2 + EDGE_MASK, 0); }
    const bool same = (uint16_t)(int16_t)s8(m.rb(a2 + EDGE_MASK)) == D2;
    if (!same && m.rb(a2 + AIR) != AIRBORNE) {
      m.wb(a2 + AIR, TEETER);
      m.ww(a2 + EDGE_CX, m.rw(CELL_X));
      m.ww(a2 + EDGE_CY, m.rw(CELL_Y));
      m.wb(a2 + EDGE_MASK, (uint8_t)D2);
      m.wl(a2 + VZ, FALL_VZ);
    }
  } else if (!D2) {
    if (m.rb(a2 + AIR) != AIRBORNE) {
      m.wb(a2 + AIR, ON_GROUND);
      m.wl(a2 + VZ, 0);
      if (s32((int64_t)m.sl(a2 + PZ) - h) > 0x60000) { startFall(m, a2); }
    }
  }

  // teetering: lift z to the highest neighbour edge, fall when none is near
  if (m.rb(a2 + AIR) != TEETER) { return; }
  const int32_t D5 = abs64(s16(m.rw(CELL_X) - m.rw(a2 + EDGE_CX)));
  const int32_t D6 = abs64(s16(m.rw(CELL_Y) - m.rw(a2 + EDGE_CY)));
  if (D5 >= 2 || D6 >= 2) { return; }

  const uint8_t mask = m.rb(a2 + EDGE_MASK);
  int32_t best = -1;
  uint16_t bestv = 0;
  const uint16_t TX = m.rw(FRAC_X), TY = m.rw(FRAC_Y);
  const uint16_t NTX = 7 - TX, NTY = 7 - TY;

  // D5/D6 == 0xffff below never holds (abs: neg.l at 0x1645a; compare with -1 at 0x16524).
  // Dead in the original too, kept for fidelity.
  for (uint8_t bit = 0; bit < 8; bit++) {
    const uint16_t iso = ((uint16_t)(int16_t)s8(mask)) & (1 << bit);
    uint16_t D1w = 5, D2w = 5, cur;
    switch (iso) {
      case 0x01:
        cur = m.rw(WALL_PX);
        if (D5 == 0) { D1w = TX; }
        break;
      case 0x02:
        cur = m.rw(WALL_PY);
        if (D6 == 0) { D1w = NTY; }
        break;
      case 0x04:
        cur = m.rw(WALL_NX);
        if (D5 == 0) { D1w = NTX; }
        break;
      case 0x08:
        cur = m.rw(WALL_NY);
        if (D6 == 0) { D1w = TY; }
        break;
      case 0x10:
        cur = m.rw(WALL_D0);
        if (!(D5 == 0xffff || D6 == 1)) { D1w = TX; D2w = NTY; }
        break;
      case 0x20:
        cur = m.rw(WALL_D1);
        if (!(D5 == 1 || D6 == 1)) { D1w = NTX; D2w = NTY; }
        break;
      case 0x40:
        cur = m.rw(WALL_D2);
        if (!(D5 == 1 || D6 == 0xffff)) { D1w = NTX; D2w = TY; }
        break;
      case 0x80:
        cur = m.rw(WALL_D3);
        if (!(D5 == 0xffff || D6 == 0xffff)) { D1w = TX; D2w = TY; }
        break;
      default:
        continue;
    }

    uint16_t ti;
    if (D1w < 5) {
      if (D1w > D2w) {
        ti = D1w;
      } else if (D2w < 5) {
        ti = D2w;
      } else {
        ti = D1w;
      }
    } else if (D2w < 5) {
      ti = D2w;
    } else {
      continue;
    }

    // 0x165ec `cmpa.w d2,a1; ble`: the smallest magnitude wins (Marble Love: the largest)
    const int8_t mag = s8(m.romb(TEETER_LIFT + ti));
    if (best == -1 || mag < best) {
      best = mag;
      bestv = cur;
    }
  }

  if (best == -1) {
    m.wb(a2 + AIR, AIRBORNE);
    m.wl(a2 + VZ, FALL_VZ);
    marble_sound::send(m, marble_sound::SND_FALL);
    return;
  }
  const int32_t diff = s16(bestv) - best;
  if (diff > 0) { m.wl(a2 + PZ, (uint32_t)m.sl(a2 + PZ) + ((uint32_t)diff << 16)); }
}

// 0x1706c: velocity push at edges
void edgepush_1706c(Mem &m, uint32_t a1) {
  uint32_t d5 = m.rl(a1 + VX), d6 = m.rl(a1 + VY);
  const int8_t d3 = s8(m.rb(FRAC_X + 1)), d2 = s8(7 - m.rb(FRAC_Y + 1));
  const int8_t d1 = s8(7 - m.rb(FRAC_X + 1)), d4 = s8(m.rb(FRAC_Y + 1));
  const int16_t mM4 = s16(m.romw(PUSH + 2 * d3));
  const int16_t mM2 = s16(-s16(m.romw(PUSH + 2 * d2)));
  const int16_t a4w = s16(-s16(m.romw(PUSH + 2 * d1)));
  const int16_t a0w = s16(m.romw(PUSH + 2 * d4));
  auto fl = [&m](uint32_t a) { const uint8_t f = m.rb(a); return f != 0 && f < 3; };

  if (fl(EDGE_PX) && d3 < 4 && m.sw(WALL_PX) > 0) { d5 += mM4; }
  if (fl(EDGE_PY) && d2 < 4 && m.sw(WALL_PY) > 0) { d6 += mM2; }
  if (fl(EDGE_NX) && d1 < 4 && m.sw(WALL_NX) > 0) { d5 += a4w; }
  if (fl(EDGE_NY) && d4 < 4 && m.sw(WALL_NY) > 0) { d6 += a0w; }

  const uint8_t bm = m.rb(DIAG_MASK);
  if ((bm & 1) && d3 < 4 && d2 < 4 && m.sw(WALL_PX) > 0) { d5 += mM4; d6 += mM2; }
  if ((bm & 2) && d2 < 4 && d1 < 4 && m.sw(WALL_PY) > 0) { d6 += mM2; d5 += a4w; }
  if ((bm & 4) && d1 < 4 && d4 < 4 && m.sw(WALL_NX) > 0) { d5 += a4w; d6 += a0w; }
  if ((bm & 8) && d4 < 4 && d3 < 4 && m.sw(WALL_NY) > 0) { d6 += a0w; d5 += mM4; }

  m.wl(a1 + VX, d5);
  m.wl(a1 + VY, d6);
}

// floor(d3 - f) at 0, unsigned compare as the 68010 (0x25e7c)
inline uint32_t subFloor(uint32_t d3, int32_t f) {
  return (uint32_t)f >= d3 ? 0 : d3 - (uint32_t)f;
}

enum Friction : int8_t { FR_NONE = -1, FR_NORMAL = 0, FR_2 = 2, FR_3 = 3, FR_4 = 4 };

// 0x25e7c: friction / vector scale
void friction_25e7c(Mem &m, uint32_t a, Friction mode) {
  const int32_t x = m.sl(a + VX), y = m.sl(a + VY);
  uint32_t d3 = speedNorm(x, y);
  const uint8_t i4 = (d3 >> 15) & 0xf, i2 = (d3 >> 12) & 7;
  const int16_t t0 = s16(m.romw(FRICTION + 2 * i4)), t1 = s16(m.romw(FRICTION + 2 * i4 + 2));
  const int16_t dd = s16(t1 - t0);
  int32_t d2l = ((dd * i2) >> 3) + t0;
  if (d3 < 0x100) { d3 = 0x100; }

  uint32_t d4f, sec = 0;
  switch (mode) {
    case FR_2:
      d2l = shl32(d2l, 2);
      d4f = subFloor(d3, d2l);
      break;
    case FR_3:
      d4f = subFloor(d3, d2l);
      d2l = add32(d2l, shl32(d2l, 2));
      sec = subFloor(d3, d2l);
      break;
    case FR_4:
      d4f = (uint32_t)(d2l >> 2) + d3;
      break;
    default:
      d4f = subFloor(d3, d2l);
      break;
  }

  const uint32_t div = (d3 >> 8) & 0xffff;
  const uint16_t q = divu(d4f << 6, div);
  const uint16_t q2 = mode == FR_3 ? (uint16_t)divu(sec << 6, div) : q;
  m.wl(a + VX, scaleV(x, q));
  m.wl(a + VY, scaleV(y, q2));
}

// 0x25df6: gravity along the terrain gradient
void gravity_25df6(Mem &m, uint32_t a) {
  for (uint32_t g = GRAD_X; g <= GRAD_Y; g += 2) {
    const int16_t v = m.sw(g);
    if ((abs64(v) & 0xffff) >= 0xc) { m.ww(g, (uint16_t)(v * 4)); }
  }
  const int32_t gx = m.sw(GRAD_X) * 2048, gy = m.sw(GRAD_Y) * 2048;
  if (m.rw(RACE) == RACE_SILLY) {
    m.wl(a + VX, add32(m.sl(a + VX), gx));
    m.wl(a + VY, add32(m.sl(a + VY), gy));
  } else {
    m.wl(a + VX, add32(m.sl(a + VX), -gx));
    m.wl(a + VY, add32(m.sl(a + VY), -gy));
  }
}

Friction frictionMode(uint8_t c) {
  switch (c) {
    case 0x2d: case 0x2e: case 0x3b:
      return FR_2;
    case 0x38: case 0x39: case 0x3a:
      return FR_4;
    case 0x2f: case 0x30: case 0x31:
      return FR_3;
    case 0x10: case 0x17: case 0x18: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36:
    case 0x37:
      return FR_NONE;
    default:
      return FR_NORMAL;
  }
}

// 0x25c74: landing (player path)
void land_25c74(Mem &m, uint32_t a2, int16_t fall) {
  const uint8_t prev = m.rb(a2 + STATE);
  if (prev == GOAL) { m.wb(a2 + SLOT, OUT_OF_PLAY); }
  m.wb(a2 + STATE, DIZZY);
  int32_t d4 = s8(m.rb(a2 + DAMAGE)) + fall;
  if (d4 > 0x7f) { d4 = 0x7f; }
  m.wb(a2 + DAMAGE, (uint8_t)d4);

  const int8_t s56 = m.sb(a2 + DIZZY_CNT), s57 = m.sb(a2 + DAMAGE);
  if (s56 + ((s57 - s56) >> 1) > BREAK_LIMIT) {
    if (prev == DIZZY || prev == EATEN) {  // broken while dizzy (sound 0x15884)
      m.wb(a2 + ANIM_5F, 0);
      m.wb(a2 + DIZZY_CNT, 1);
      m.wb(a2 + ANIM_60, ANIM_RATE);
      m.wl(a2 + ANIM, ANIM_DIZZY_END);
    }

    // shatter on screen (sy of the last projection), else lost
    const int16_t sy = m.sw(a2 + SCREEN + 2);
    if (sy > BREAK_SY_MIN && sy < BREAK_SY_MAX) {
      enter_25bae(m, a2, BROKEN);
      return;
    }
    m.wb(a2 + DAMAGE, FELL);
    enter_25bae(m, a2, LOST);
    return;
  }
  if (prev == DIZZY) { return; }
  m.wb(a2 + ANIM_5F, 0);
  m.wb(a2 + ANIM_60, ANIM_RATE);
  m.wl(a2 + ANIM, ANIM_DIZZY);
  marble_sound::send(m, marble_sound::SND_DIZZY);
}

// 0x13966: zone hook (respawn and zone change): silly and aerial sound latches (sounds not
// ported), aerial 0x400768 and pistons 0x186ac, zone wait slots of the new zone restarted
void zoneHook_13966(Mem &m, uint32_t a2) {
  static const uint8_t SILLY_A = 5, SILLY_B = 6, AERIAL_A = 0x14, AERIAL_B = 6, AERIAL_C = 7;
  static const uint16_t MODE3_TICKS = 0x3c;
  const uint16_t race = m.rw(RACE);
  const uint8_t zone = m.rb(a2 + ZONE), flags = m.rb(ZONE_SOUNDS);
  if (race == RACE_SILLY) {
    if (zone == SILLY_A && !(flags & 1)) {
      m.wb(ZONE_SOUNDS, flags | 1);  // sound 0x15
    } else if (zone == SILLY_B && !(flags & 2)) {
      m.wb(ZONE_SOUNDS, flags | 2);  // sound 0x16
    }
  }
  if (race != RACE_AERIAL) { return; }

  if (zone == AERIAL_A && !(flags & 1)) {
    m.wb(ZONE_SOUNDS, flags | 1);  // sound 0xf
  } else if ((zone == AERIAL_B || zone == AERIAL_C) && !(flags & 2)) {
    m.wb(ZONE_SOUNDS, flags | 2);  // sound 0x12
    m.ww(MODE3_TIMER, MODE3_TICKS);
  }
  marble_enemies::arm_186ac(m);
  marble_objects::zoneScripts(m, zone);
}

// 0x1365c: zone lines (checkpoint zone +0x1b, goal zone 0xff); aerial zone 4 entry resets its
// slots (0x13892); a changed zone runs the zone hook 0x13966
void zones_1365c(Mem &m, uint32_t a2) {
  const uint16_t race = m.rw(RACE);
  uint32_t a0 = m.roml(ZONE_LISTS + 4 * race);
  const int16_t ncx = m.sw(CELL_X), ncy = m.sw(CELL_Y);
  const int16_t ocx = m.sw(OLD_CX), ocy = m.sw(OLD_CY);
  if (ncx == ocx && ncy == ocy) { return; }

  const uint8_t d2 = m.rb(a2 + ZONE);
  for (;; a0 += 6) {
    // line entry: x, y, end, zone a, zone b; x < 0: line y = |y| from x = |x| to end
    const int8_t b0 = s8(m.romb(a0)), b1 = s8(m.romb(a0 + 1)), end = s8(m.romb(a0 + 2));
    const bool horiz = b0 < 0;
    const int16_t X = b0 < 0 ? -b0 : b0, Y = b1 < 0 ? -b1 : b1;
    const bool hit = horiz ? (Y == ncy || Y == ocy) : (X == ncx || X == ocx);
    if (hit) {
      bool onNew, onOld;
      if (horiz) {
        onNew = ncy == Y && ncx >= X && ncx <= end;
        onOld = ocy == Y && ocx >= X && ocx <= end;
      } else {
        onNew = ncx == X && ncy >= Y && ncy <= end;
        onOld = ocx == X && ocy >= Y && ocy <= end;
      }
      if (onOld && !onNew) {
        const bool sideA = horiz ? ncy <= Y : !(ncx > X);
        m.wb(a2 + ZONE, m.romb(a0 + (sideA ? 3 : 4)));
        if (m.rb(a2 + ZONE) == 0xff) {
          // 0x137a4: the first player at the goal scores race * 2 + 3 (palette flash not
          // ported); the race timer stops, +0x6d keeps its frame count
          if (m.rb(GOAL_FIRST) == 0xff && m.rb(a2 + SLOT) != OUT_OF_LIST) {
            m.wb(GOAL_FIRST, m.rb(a2 + PLAYER_NO));
            score(m, a2, (uint8_t)(race * 2 + 3));
          }
          m.wb(a2 + TIMER_KEPT, m.rb(a2 + TIMER_TICKS));
          m.wb(a2 + TIMER_TICKS, 0xff);
          m.wb(a2 + STATE, GOAL);
          m.wb(a2 + DAMAGE, 0x1e);
          if (race == RACE_PRACTICE && (d2 == 3 || d2 == 4)) {
            m.wl(a2 + VX, 0);
            m.wl(a2 + VY, 0);
          } else {
            m.wl(a2 + VX, (uint32_t)(int32_t)s8(m.romb(GOAL_VX + race)) << 16);
            m.wl(a2 + VY, (uint32_t)(int32_t)s8(m.romb(GOAL_VY + race)) << 16);
          }
        } else if (m.rb(a2 + ZONE) == ZONE_SLOTS && race == RACE_AERIAL) {
          marble_objects::zone4_13892(m);
        }
        if (m.rb(a2 + ZONE) != d2) { zoneHook_13966(m, a2); }
        return;
      }
    }
    if (m.romb(a0 + 3) == 0xff && m.romb(a0 + 4) == 0xff) { return; }
  }
}

// 0x121b8: object physics update (player, steelies)
void physics_121b8(Mem &m, uint32_t a2) {
  m.wl(OLD_X, m.rl(a2 + PX));
  m.wl(OLD_Y, m.rl(a2 + PY));
  m.wl(OLD_Z, m.rl(a2 + PZ));
  m.ww(OLD_CX, (uint16_t)(m.sl(OLD_X) >> 19));
  m.ww(OLD_CY, (uint16_t)(m.sl(OLD_Y) >> 19));
  m.ww(CELL_Y, 0xffff);
  m.ww(CELL_X, 0xffff);
  posupd_1bab2(m, a2);

  // more than 16 px below the terrain: embedded, lost (sounds 0x15884, 0x46); a steelie is
  // freed and scored
  const bool pl = isPlayer(a2);
  if (s32((int64_t)height_1cc62(m) - m.sl(a2 + PZ)) > 0x100000) {
    if (!pl) {
      marble_enemies::remove_15bd0(m, a2, marble_enemies::SCORE, marble_enemies::CLEAR);
      return;
    }
    m.wb(a2 + DAMAGE, EMBEDDED);
    enter_25bae(m, a2, LOST);
    return;
  }

  for (uint8_t o = 0; o <= 8; o += 4) { m.wl(a2 + PX + o, add32(m.sl(a2 + PX + o), m.sl(a2 + o))); }
  posupd_1bab2(m, a2);
  edges_1c676(m);

  // side bounds of the screen: swap vx/vy and move along the border
  const int32_t d4 = m.sl(a2 + VX), d5 = m.sl(a2 + VY);
  const int16_t d1 = s16(m.rw(POS_Y) - m.rw(POS_X) + 0x88);
  if ((d1 < 4 && d4 > d5) || (d1 > 0x11c && d5 > d4)) {
    m.wl(a2 + VX, (uint32_t)d5);
    m.wl(a2 + VY, (uint32_t)d4);
    m.wl(a2 + PX, m.rl(OLD_X));
    m.wl(a2 + PY, m.rl(OLD_Y));
    const int32_t h = s32(((int64_t)d5 + d4) >> 1);
    m.wl(a2 + PX, add32(m.sl(a2 + PX), h));
    m.wl(a2 + PY, add32(m.sl(a2 + PY), h));
    posupd_1bab2(m, a2);
  }

  m.wb(HIT_Y, 0);
  m.wb(HIT_X, 0);
  walls_1b5c2(m, a2);
  if (m.rb(HIT_X) || m.rb(HIT_Y)) { posupd_1bab2(m, a2); }
  m.wb(HIT_Y, 0);
  m.wb(HIT_X, 0);
  marble_objects::slots_29cce(m, a2);
  if (m.rb(a2 + SURFACE) == 0xa || m.rb(a2 + STATE) == LOST) { return; }

  // 0x1239c: cell refresh after slot hits, 0x1bc88 object pair collision; then the player
  // only contacts (slinkies 0x14e92, acid 0x175c8, pistons 0x1881c, minis 0x1924e, birds
  // 0x19d94) and zones; a carried player skips the zones
  if (m.rb(HIT_X) || m.rb(HIT_Y)) { posupd_1bab2(m, a2); }
  if (marble_enemies::pair_1bc88(m, a2)) { posupd_1bab2(m, a2); }
  if (pl) {
    marble_enemies::contact_14e92(m, a2);
    if (marble_enemies::contact_175c8(m, a2)) { posupd_1bab2(m, a2); }
    if (marble_enemies::contact_1881c(m, a2)) { posupd_1bab2(m, a2); }
    marble_enemies::contact_1924e(m, a2);
    marble_enemies::contact_19d94(m, a2);
    if (m.rb(a2 + STATE) == CARRIED) { return; }
    zones_1365c(m, a2);
  }

  const int32_t h = height_1cc62(m);
  edge_160f6(m, a2, h);

  // z changed more than 8 px on a flat surface code: start a fall
  const int64_t dz = abs64(s32((int64_t)m.sl(a2 + PZ) - m.sl(OLD_Z)));
  if (dz > 0x80000 && m.rb(a2 + SURFACE) == 0 && m.sl(a2 + VZ) <= 0) { startFall(m, a2); }

  if (m.rb(a2 + AIR) == AIRBORNE) {
    if (h >= m.sl(a2 + PZ)) {
      m.wb(a2 + AIR, ON_GROUND);
      m.wl(a2 + VZ, 0);
      const int64_t fall = abs64(s32((int64_t)m.sl(a2 + GROUND_Z) - h)) >> 15;
      m.wl(a2 + PZ, (uint32_t)h);
      if (pl) {
        land_25c74(m, a2, s16((int32_t)fall));
      } else {
        marble_enemies::land_25c74(m, a2, s16((int32_t)fall));
      }
    } else if (s32((int64_t)m.sl(a2 + GROUND_Z) - m.sl(a2 + PZ)) > 0x800000) {
      if (!pl) {  // fell more than 128 px: a steelie is freed and scored
        marble_enemies::remove_15bd0(m, a2, marble_enemies::SCORE, marble_enemies::CLEAR);
        return;
      }
      m.wb(a2 + DAMAGE, FELL);  // fell more than 128 px: lost (sounds 0x15884, 0x46)
      enter_25bae(m, a2, LOST);
      return;
    }
  } else if (m.rb(a2 + AIR) == ON_GROUND) {
    m.wl(a2 + PZ, (uint32_t)h);
  }

  // 0x1b9cc projection, 0x1281c occlusion (0x1c014 surface dots: not ported)
  proj_1b9cc(m, a2, KEEP_CELL);
  marble_occlusion::enter(m, a2);
  edgepush_1706c(m, a2);
  if (m.rb(a2 + AIR)) { return; }

  const Friction fm = frictionMode(m.rb(a2 + SURFACE));
  if (fm != FR_NONE) { friction_25e7c(m, a2, fm); }
  if (pl && m.rw(GAME_STATE) != marble_flow::ATTRACT) { gravity_25df6(m, a2); }
}

// 0x26214: +0xc4 sums the friction table at the speed (>> 9); every SCORE_STEP scores
// SCORE_POINTS (0x28608: +0xbc, flag 0x40039c)
void distance_26214(Mem &m, uint32_t a2, uint32_t speed) {
  const uint8_t i4 = (speed >> 15) & 0xf, i2 = (speed >> 12) & 7;
  const int16_t t0 = s16(m.romw(FRICTION + 2 * i4)), t1 = s16(m.romw(FRICTION + 2 * i4 + 2));
  const int32_t d1 = ((s16(t1 - t0) * i2 >> 3) + t0) >> 9;
  m.ww(a2 + DISTANCE, (uint16_t)(m.rw(a2 + DISTANCE) + d1));
  if (m.sw(a2 + DISTANCE) <= SCORE_STEP) { return; }

  m.ww(a2 + DISTANCE, m.rw(a2 + DISTANCE) - SCORE_STEP);
  m.wl(a2 + SCORE, m.rl(a2 + SCORE) + SCORE_POINTS);
  m.wb(SCORED, m.rb(SCORED) | (1 << m.rb(a2 + PLAYER_NO)));
}

// 0x26196 / 0x261bc: speed cap
void speedcap_26196(Mem &m, uint32_t a2) {
  const uint32_t cap = m.rb(a2 + STATE) == ROLL ? 0x40000 : 0x50000;
  const uint32_t d3 = speedNorm(m.sl(a2 + VX), m.sl(a2 + VY));
  if (a2 == PLAYER || a2 == PLAYER + PLAYER_SIZE) { distance_26214(m, a2, d3); }  // players
  if (cap >= d3) { return; }

  const uint16_t q = divu(cap << 6, (d3 >> 8) & 0xffff);
  m.wl(a2 + VX, scaleV(m.sl(a2 + VX), q));
  m.wl(a2 + VY, scaleV(m.sl(a2 + VY), q));
}

// attract demo (MARBLE.md "Demo race"): waypoint lists, banners
static const uint32_t BANNERS = 0x242aa;        // long per banner: alpha string chain
static const uint16_t BANNER_FRAMES = 0x5a;     // BIOS 0x12a: frames shown
static const int32_t WAYPOINT_BIAS = 0x40000;   // record cell to its centre, 16.16
static const uint16_t WAYPOINT_NEAR = 0x20;     // |delta| >> 12 below: passed
static const uint16_t SLOW_BELOW = 0x40;        // distance below: speed SLOW_SPEED
static const int32_t SLOW_SPEED = 0xc000;
static const int32_t FALL_STEP = -0x6000, FALL_MAX = -0x50000;  // airborne vz

// 0x12a (BIOS, state-machine-schedule.ts scheduleStateMachine1): message slot 0 shows banner
// b's chain for BANNER_FRAMES frames; marble_flow erases it
void banner_12a(Mem &m, uint8_t b) {
  m.wl(MSG_CHAIN, m.roml(BANNERS + 4 * b));
  m.wb(MSG_STATE, 1);
  m.ww(MSG_FRAMES, BANNER_FRAMES);
  m.ww(MSG_COUNT, 0);
}

// 68k divs.w: low word of d / q, d unchanged on overflow
int32_t divsw(int32_t d, uint16_t q) {
  const int32_t v = d / s16(q);
  if (v < -0x8000 || v > 0x7fff) { return d; }
  return (int32_t)(((uint32_t)d & 0xffff0000) | (uint16_t)v);
}

// 0x1815a: pass the records within reach (banner, list end), then ease +0x00/+0x04 towards
// the next record's cell centre at its speed, gravity when airborne, speed cap
void waypoint_1815a(Mem &m, uint32_t a2) {
  int32_t d2, d3, d5;
  uint16_t d4, d6;
  for (;;) {
    uint32_t a3 = m.rl(WAYPOINT);
    if (!m.romb(a3)) { return; }

    d2 = s32((int64_t)(s8(m.romb(a3)) * (1 << 19)) - m.sl(a2 + PX) + WAYPOINT_BIAS);
    d3 = s32((int64_t)(s8(m.romb(a3 + 1)) * (1 << 19)) - m.sl(a2 + PY) + WAYPOINT_BIAS);
    d5 = s8(m.romb(a3 + 2)) * (1 << 16);
    d4 = (uint16_t)(abs64(d2) >> 12);
    d6 = (uint16_t)(abs64(d3) >> 12);
    if (d4 >= WAYPOINT_NEAR || d6 >= WAYPOINT_NEAR) { break; }

    if (s8(m.romb(a3 + 3)) >= 0) { banner_12a(m, m.romb(a3 + 3)); }
    a3 += 4;
    if (!m.romb(a3)) {
      m.ww(WAYPOINTS_END, 1);
      m.wb(a2 + TIMER_TICKS, 0xff);  // timer stopped
    }
    m.wl(WAYPOINT, a3);
  }

  const uint16_t lo = d4 <= d6 ? d4 : d6, hi = d4 <= d6 ? d6 : d4;
  const uint16_t d1 = (uint16_t)((lo >> 3) * 3 + hi);
  if (d1) {
    d2 = divsw(d2, d1);
    d3 = divsw(d3, d1);
  } else {
    d2 = (int32_t)((uint32_t)d2 & 0xffff0000);
    d3 = (int32_t)((uint32_t)d3 & 0xffff0000);
  }
  if (d1 < SLOW_BELOW) { d5 = SLOW_SPEED; }
  const int16_t k = s16(d5 >> 8);
  d2 = (k * s16(d2)) >> 4;
  d3 = (k * s16(d3)) >> 4;
  m.wl(a2 + VX, add32(m.sl(a2 + VX), s32((int64_t)d2 - m.sl(a2 + VX)) >> 3));
  m.wl(a2 + VY, add32(m.sl(a2 + VY), s32((int64_t)d3 - m.sl(a2 + VY)) >> 3));
  if (m.rb(a2 + AIR)) {
    int64_t vz = (int64_t)m.sl(a2 + VZ) + FALL_STEP;
    if (vz < FALL_MAX) { vz = FALL_MAX; }
    m.wl(a2 + VZ, (uint32_t)vz);
  }
  speedcap_26196(m, a2);
}

bool inAttract(const Mem &m) { return m.rw(marble_flow::GAME_STATE) == marble_flow::ATTRACT; }

// surface codes where 0x17f66 lets the trackball steer
bool steerSurface(uint8_t c) {
  return c == 0 || c == 0x3b || c == 0x2d || c == 0x2e || (c >= 0x38 && c <= 0x3a) ||
         (c >= 0x2f && c <= 0x31);
}

// 0x17f66: input -> velocity (attract: the waypoint driver)
void input_17f66(Mem &m, uint32_t a2) {
  const uint8_t slot = m.rb(a2 + SLOT);
  if (slot == 2 || slot == 3) { return; }

  if (inAttract(m)) {
    waypoint_1815a(m, a2);
    return;
  }

  const uint8_t st = m.rb(a2 + STATE);
  if (m.rb(a2 + AIR) != AIRBORNE && steerSurface(m.rb(a2 + SURFACE))) {
    int32_t d3 = m.sb(a2 + BALL_C7) * 0x160;
    int32_t d2 = -m.sb(a2 + BALL_C6) * 0x160;
    if (st == 1 || st == 5) {
      int32_t d1 = 0x1f - m.sb(a2 + DIZZY_CNT);
      if (st == 5 && d1 < 4) { d1 = 4; }
      d3 = shl32(s16(d3 >> 8) * s16(d1), 3);
      d2 = shl32(s16(d2 >> 8) * s16(d1), 3);
    }
    m.wl(a2 + VX, add32(m.sl(a2 + VX), d3));
    m.wl(a2 + VY, add32(m.sl(a2 + VY), d2));
  } else if (m.rb(a2 + AIR)) {
    int64_t vz = (int64_t)m.sl(a2 + VZ) - 0x6000;
    if (vz < -0x50000) { vz = -0x50000; }
    m.wl(a2 + VZ, (uint32_t)vz);
  }
  speedcap_26196(m, a2);
}

// 0x253bc: remember the last ground cell, z and zone
void ground_253bc(Mem &m, uint32_t a) {
  if (m.rb(a + AIR) != ON_GROUND) { return; }
  m.ww(a + GROUND_CX, (uint16_t)(m.sl(a + PX) >> 19));
  m.ww(a + GROUND_CY, (uint16_t)(m.sl(a + PY) >> 19));
  m.wl(a + GROUND_Z, m.rl(a + PZ));
  m.wb(a + GROUND_ZONE, m.rb(a + ZONE));
}

// Dynamic terrain codes at race start. Intermediate: level init 0x105be calls 0x11ac2,
// 66 words from 0x1d370. Practice, beginner, ultimate: a level object animation (Phase 4)
// writes them, held at its first frame (the arcade writes beginner and ultimate only once
// the objects spawn). Aerial, silly: none.
void dyn_init(Mem &m, uint8_t race) {
  if (race == RACE_INTERMEDIATE) {
    for (uint8_t k = 0; k < DYN_INTERMEDIATE_N; k++) {
      m.ww(DYN + 2 * k, m.romw(DYN_INTERMEDIATE + 2 * k));
    }
    return;
  }

  uint32_t a;
  switch (race) {
    case RACE_PRACTICE: a = DYN_PRACTICE; break;
    case RACE_BEGINNER: a = DYN_BEGINNER; break;
    case RACE_ULTIMATE: a = DYN_ULTIMATE; break;
    default: return;
  }
  const uint16_t slot = m.romw(a), n = m.romw(a + 2);
  for (uint16_t k = 0; k < n; k++) { m.ww(DYN + 2 * (slot + k), m.romw(a + 4 + 2 * k)); }
}

// 0x259b4: race start of player 1, physics fields only (sprite setup skipped)
void start_259b4(Mem &m, uint8_t race, uint32_t a2) {
  m.ww(RACE, race);
  dyn_init(m, race);

  // start cell: high byte x, low byte y; position = cell * 8 + 4
  const uint16_t cell = m.romw(m.desc() + DESC_CELL);
  m.wl(a2 + PX, ((uint32_t)(cell >> 8) << 19) + 0x40000);
  m.wl(a2 + PY, ((uint32_t)(cell & 0xff) << 19) + 0x40000);
  m.ww(CELL_Y, 0xffff);
  m.ww(CELL_X, 0xffff);
  posupd_1bab2(m, a2);
  m.wl(a2 + PZ, (uint32_t)height_1cc62(m));

  m.wb(a2 + ZONE, 0);
  static const uint8_t longs[] = {VX, VY, VZ, ANIM};
  static const uint8_t bytes[] = {DIZZY_CNT, DAMAGE, STATE, AIR, SURFACE};
  for (uint8_t o : longs) { m.wl(a2 + o, 0); }
  for (uint8_t o : bytes) { m.wb(a2 + o, 0); }
  m.wb(a2 + SLOT, 1);
  m.wb(a2 + SCORE_TEXT_ON, 0xff);  // palette pulses off (0x26bee, 0x26c78)
  m.wb(a2 + PULSE_FLASH, 0xff);
  if (race == RACE_AERIAL) {  // starts on surface 0x17
    m.wb(a2 + SURFACE, 0x17);
    m.wb(a2 + SURFACE_T, 0xff);
  }
}

// 0x17cb8: kind (HIT_*) of another live object near pixel (x, y), index to BLOCK_HIT, 0 if
// none. Players (not a2), steelies, slinkies: both deltas below 32 px and the octant
// distance (deltas * 16) below r.
uint8_t blocked_17cb8(Mem &m, uint32_t a2, int16_t x, int16_t y, uint16_t r) {
  struct Group { uint32_t base; uint16_t size; uint8_t n, kind; };
  const Group groups[] = {{PLAYER, PLAYER_SIZE, (uint8_t)m.rw(PLAYERS), HIT_PLAYER},
                          {STEELIES, STEELIE_SIZE, STEELIE_N, HIT_STEELIE},
                          {SLINKIES, SLINKY_SIZE, SLINKY_N, HIT_SLINKY}};
  for (const Group &g : groups) {
    for (uint8_t i = 0; i < g.n; i++) {
      const uint32_t a = g.base + (uint32_t)g.size * i;
      if ((g.kind == HIT_PLAYER && a == a2) || !m.rb(a + SLOT)) { continue; }

      const int16_t ex = s16(m.rw(a + PX) - (uint16_t)x), ey = s16(m.rw(a + PY) - (uint16_t)y);
      const uint16_t dx = (uint16_t)(ex < 0 ? -ex : ex), dy = (uint16_t)(ey < 0 ? -ey : ey);
      if (dx >> 3 > NEAR_CELLS || dy >> 3 > NEAR_CELLS) { continue; }

      const uint16_t fx = (uint16_t)(dx << 4), fy = (uint16_t)(dy << 4);
      const uint32_t d = fx > fy ? (uint32_t)(fy >> 3) * 3 + fx : (uint32_t)(fx >> 3) * 3 + fy;
      if (r <= d) { continue; }
      m.wl(BLOCK_HIT, i);
      return g.kind;
    }
  }
  return 0;
}

// checkpoint entry a3: spawn pixel (cell * 8 + 4)
inline int16_t spawnX(const Mem &m, uint32_t a3) { return (int16_t)(m.romb(a3) * 8 + 4); }
inline int16_t spawnY(const Mem &m, uint32_t a3) { return (int16_t)(m.romb(a3 + 1) * 8 + 4); }

// checkpoint entry a3 -> spawn pixel and zone
void spawnAt(Mem &m, uint32_t a3) {
  m.wl(SPAWN_X, (uint32_t)spawnX(m, a3));
  m.wl(SPAWN_Y, (uint32_t)spawnY(m, a3));
  m.wb(SPAWN_ZONE, m.romb(a3 + 2));
}

// 0x2637a: nearest free checkpoint of the ground zone (+0x1d) to the last ground cell
void nearest_2637a(Mem &m, uint32_t a2) {
  const uint16_t zone = (uint16_t)(int16_t)m.sb(a2 + GROUND_ZONE);  // ext.w
  uint32_t best = SPAWN_RANGE;
  for (uint32_t a3 = m.roml(CHECKPOINTS + 4 * m.rw(RACE)); m.romb(a3) != 0xff; a3 += 4) {
    if (m.romb(a3 + 2) != zone) { continue; }

    // octagonal distance of the cell deltas, words << 4
    const int16_t ex = s16(m.rw(a2 + GROUND_CX) - m.romb(a3));
    const int16_t ey = s16(m.rw(a2 + GROUND_CY) - m.romb(a3 + 1));
    const uint16_t dx = (uint16_t)((ex < 0 ? -ex : ex) << 4);
    const uint16_t dy = (uint16_t)((ey < 0 ? -ey : ey) << 4);
    const uint32_t d = dx > dy ? (uint32_t)(dy >> 3) * 3 + dx : (uint32_t)(dx >> 3) * 3 + dy;

    if (blocked_17cb8(m, a2, spawnX(m, a3), spawnY(m, a3), SPAWN_FREE)) { continue; }
    if (d >= best) { continue; }
    best = d;
    spawnAt(m, a3);
  }
}

// 0x262b2: respawn point; fallback is the last free checkpoint of zone - 1, none: pixel
// (0, 0), zone NO_ZONE
void spawnPoint_262b2(Mem &m, uint32_t a2) {
  m.wl(SPAWN_Y, 0);
  m.wl(SPAWN_X, 0);
  m.wb(SPAWN_ZONE, NO_ZONE);
  nearest_2637a(m, a2);
  if (m.rb(SPAWN_ZONE) != NO_ZONE) { return; }

  const uint32_t first = m.roml(CHECKPOINTS + 4 * m.rw(RACE));
  uint32_t a3 = first;
  while (m.romb(a3) != 0xff) { a3 += 4; }
  int16_t zone = m.sb(a2 + GROUND_ZONE);
  if (zone) { zone--; }

  // backwards from the end marker (its zone 0xff never matches, 0x17cb8 still runs)
  for (; a3 != first - 4; a3 -= 4) {
    if (blocked_17cb8(m, a2, spawnX(m, a3), spawnY(m, a3), SPAWN_FREE)) { continue; }
    if (m.romb(a3 + 2) == (uint16_t)zone) {
      spawnAt(m, a3);
      return;
    }
  }
}

// 0x1b9cc (see marble_physics.h, sx/sy) for object a2; REFETCH reloads the terrain cell
void proj_1b9cc(Mem &m, uint32_t a2, Refetch r) {
  if (r == REFETCH) {
    m.ww(CELL_Y, 0xffff);
    m.ww(CELL_X, 0xffff);
  }
  posupd_1bab2(m, a2);
  uint16_t z = m.rw(a2 + PZ);
  const uint8_t c = m.rb(a2 + SURFACE);
  if (c >= 0x2f && c <= 0x31 && (m.rb(a2 + PX + 1) & 1) && abs64(m.sl(a2 + VX)) > 0x4000) {
    z += 2;  // bumpy surface: 2 px jitter
  }
  const int32_t xy = (int32_t)m.sw(a2 + PX) + m.sw(a2 + PY);
  const uint16_t sxw = (uint16_t)(m.rw(a2 + PY) - m.rw(a2 + PX) + 0x88);
  const uint16_t syw = (uint16_t)(m.rw(CAMERA) + z + 0x54 - (uint16_t)(xy >> 1));
  const uint32_t packed = (uint32_t)sxw << 16 | syw;
  if (packed == m.rl(a2 + SCREEN) || packed == m.rl(a2 + SCREEN_OLD)) { return; }

  m.wl(a2 + SCREEN_OLD2, m.rl(a2 + SCREEN_OLD));
  m.wl(a2 + SCREEN_OLD, m.rl(a2 + SCREEN));
  m.wl(a2 + SCREEN, packed);
}

// 0x2591a: to the respawn point 0x262b2, z on the terrain, motion cleared, projected. Not
// ported: dot pose 0x25b40 (dots 0x1c014), zone hook 0x13966 (silly, aerial).
void respawn_2591a(Mem &m, uint32_t a2) {
  spawnPoint_262b2(m, a2);
  m.wl(a2 + PX, m.rl(SPAWN_X) << 16);
  m.wl(a2 + PY, m.rl(SPAWN_Y) << 16);
  m.ww(CELL_Y, 0xffff);
  m.ww(CELL_X, 0xffff);
  posupd_1bab2(m, a2);
  m.wl(a2 + PZ, (uint32_t)height_1cc62(m));
  m.wb(a2 + ZONE, m.rb(SPAWN_ZONE));

  static const uint8_t longs[] = {VZ, VY, VX, SCREEN_OLD2, SCREEN_OLD};
  static const uint8_t bytes[] = {DIZZY_CNT, AIR, SURFACE};
  for (uint8_t o : longs) { m.wl(a2 + o, 0); }
  for (uint8_t o : bytes) { m.wb(a2 + o, 0); }
  proj_1b9cc(m, a2, KEEP_CELL);
  zoneHook_13966(m, a2);
}

// 0x25bae: state entry, velocity x/y cleared. BROKEN: shatter animation. LOST: respawn at
// once, the sparkle of state 4 shows the new spot (sound 0x3c / 0x3d). Not ported: 9
// dissolve (acid, Phase 4b).
void enter_25bae(Mem &m, uint32_t a2, State code) {
  m.wl(a2 + VY, 0);
  m.wl(a2 + VX, 0);
  if (m.rb(a2 + STATE) == GOAL) { m.wb(a2 + SLOT, OUT_OF_PLAY); }

  if (code == BROKEN) {
    marble_sound::send(m, marble_sound::SND_BREAK);
    m.wb(a2 + ANIM_5F, 0);
    m.wb(a2 + ANIM_60, ANIM_RATE);
    m.wl(a2 + ANIM, ANIM_SHATTER);
    m.wb(a2 + DIZZY_CNT, SHATTER_CNT);
    m.wb(a2 + STATE, BROKEN);
    return;
  }
  if (code == DISSOLVING) {
    m.wb(a2 + ANIM_5F, 0);
    m.wb(a2 + ANIM_60, DISSOLVE_RATE);
    m.wl(a2 + ANIM, ANIM_DISSOLVE);
    m.wb(a2 + STATE, DISSOLVING);
    return;
  }

  respawn_2591a(m, a2);
  const bool embedded = m.rb(a2 + DAMAGE) == EMBEDDED;
  marble_sound::send(m, embedded ? marble_sound::SND_LOST_EMBEDDED : marble_sound::SND_LOST);
  m.wl(a2 + ANIM, 0);
  m.wb(a2 + STATE, LOST);
  m.ww(a2 + DEATHS, m.rw(a2 + DEATHS) + 1);
}

// shatter list entry of +0x5a
inline uint16_t shatterIdx(uint32_t anim) { return (uint16_t)((anim - ANIM_SHATTER) >> 2); }

// 0x25fc2: next frame of +0x5a every +0x60 ticks (counter +0x5f). Shatter frame 9 starts the
// overlay +0x62, stepped every tick after. List end: dizzy lists loop, a shatter is lost.
void anim_25fc2(Mem &m, uint32_t a2) {
  const uint8_t st = m.rb(a2 + STATE);
  const uint32_t anim = m.rl(a2 + ANIM);
  if (st == BROKEN && anim > ANIM_SHATTER && anim < ANIM_SHATTER + SHATTER_BYTES &&
      s16(shatterIdx(anim)) > SHATTER_SHARDS) {
    m.wb(a2 + SHARD_TICK, m.rb(a2 + SHARD_TICK) + 1);
    if (m.rb(a2 + SHARD_TICK) == 1) {
      m.wb(a2 + SHARD_TICK, 0);
      m.wl(a2 + SHARDS, m.rl(a2 + SHARDS) + 4);
    }
  }

  m.wb(a2 + ANIM_5F, m.rb(a2 + ANIM_5F) + 1);
  if (m.sb(a2 + ANIM_60) > m.sb(a2 + ANIM_5F)) { return; }
  m.wb(a2 + ANIM_5F, 0);
  m.wl(a2 + ANIM, m.rl(a2 + ANIM) + 4);
  if (st == BROKEN && shatterIdx(m.rl(a2 + ANIM)) == SHATTER_SHARDS) {
    marble_sound::send(m, marble_sound::SND_SHARDS);
    m.wl(a2 + SHARDS, ANIM_SHARDS);
    m.wb(a2 + SHARD_TICK, 0);
    m.wb(a2 + SHARDS_ON, 1);
  }
  if (m.roml(m.rl(a2 + ANIM)) != LIST_END) { return; }

  if (st == DIZZY || st == EATEN) {
    m.wb(a2 + ANIM_5F, 0);
    m.wb(a2 + ANIM_60, ANIM_RATE);
    m.wl(a2 + ANIM, m.sb(a2 + DIZZY_CNT) > DIZZY_LOOPS ? ANIM_DIZZY_LOOP : ANIM_DIZZY_END);
    return;
  }
  if (st == BROKEN && m.rb(a2 + DIZZY_CNT) == 0) {
    m.wb(a2 + ANIM_5F, 0);
    m.wb(a2 + ANIM_60, ANIM_RATE);
    m.wl(a2 + ANIM, ANIM_DIZZY_END);
    m.wb(a2 + DIZZY_CNT, 1);
    return;
  }
  if (st == BROKEN && m.rb(a2 + DIZZY_CNT) == 1) {  // sound 0x15884
    enter_25bae(m, a2, BROKEN);
    return;
  }
  if (st == BROKEN) {
    m.wb(a2 + SHARDS_ON, 0);
    if (isPlayer(a2)) { m.ww(a2 + STARS, 0); }  // +0xa4 is past a steelie record
    if (m.rb(a2 + SLOT) == OUT_OF_LIST) {       // shattered steelie: off the draw list
      m.wb(a2 + SLOT, 0);
      marble_drawlist::remove(m, a2, TYPE_STEELIE);
      return;
    }
  }
  m.wb(a2 + DAMAGE, EMBEDDED);
  enter_25bae(m, a2, LOST);
}

// (cos, sin) words of angle a (0 .. FULL_TURN - 1), 0x4000 = 1
void sinePair(const Mem &m, int16_t a, int16_t &cs, int16_t &sn) {
  auto t = [&m](int16_t i) { return s16(m.romw(SINE + 2 * i)); };
  if (a <= QUARTER) {
    cs = t(a);
    sn = t(QUARTER - a);
  } else if (a < 2 * QUARTER + 1) {
    cs = s16(-t(2 * QUARTER - a));
    sn = t(a - QUARTER);
  } else if (a <= 3 * QUARTER + 1) {
    cs = s16(-t(a - 2 * QUARTER - 1));
    sn = s16(-t(3 * QUARTER + 1 - a));
  } else {
    cs = t(FULL_TURN - 1 - a);
    sn = s16(-t(a - 3 * QUARTER - 1));
  }
}

// sparkle length by +0x57 at the state 4 entry, 0: running
uint8_t orbitTicks(uint8_t damage) {
  switch (damage) {
    case FELL: return ORBIT_FELL;
    case EMBEDDED: return ORBIT_EMBEDDED;
    case CARRIED_DAMAGE: return ORBIT_CARRIED;
    default: return 0;
  }
}

// 0x13ade: respawn sparkle, 8 stars circling the screen position on a shrinking radius; the
// first 4 on screen to +0xa4, the next 4 to +0x38 (no mask bit). True when +0x57 is 0.
bool orbit_13ade(Mem &m, uint32_t a2) {
  const uint8_t ticks = orbitTicks(m.rb(a2 + DAMAGE));
  if (ticks) {
    m.wb(a2 + DAMAGE, ticks);
    m.ww(a2 + ORBIT_ANGLE, 0);
  }
  int16_t d1 = m.sb(a2 + DAMAGE);
  m.wb(a2 + DAMAGE, m.rb(a2 + DAMAGE) - 1);
  if (m.rb(a2 + STATE) == CARRIED) { d1 = ORBIT_CARRIED - d1; }
  const int32_t rad = d1 >> 1;

  uint16_t d3 = m.rw(a2 + ORBIT_ANGLE) + ORBIT_STEP;
  if (s16(d3) >= FULL_TURN) { d3 -= FULL_TURN; }
  m.ww(a2 + ORBIT_ANGLE, d3);

  const uint16_t sx = m.rw(a2 + SCREEN), sy = m.rw(a2 + SCREEN + 2);
  for (uint8_t k = 0; k < STAR_SLOTS; k++) {
    m.ww(a2 + MASKS + 6 * k, 0);
    m.ww(a2 + STARS + 6 * k, 0);
  }

  uint8_t n = 0;
  for (uint8_t k = 0; k < STAR_N; k++) {
    int16_t cs, sn;
    sinePair(m, s16(d3), cs, sn);
    const int16_t a1 = s16((uint32_t)((rad * cs) >> 12));
    const int32_t d0 = (rad * sn) >> 12;
    const int16_t x = s16((uint32_t)(s8(m.romb(STAR_STEPS + 2 * k)) + s16((uint32_t)(d0 + sx - a1))));
    const int16_t y = s16((uint32_t)(s8(m.romb(STAR_STEPS + 2 * k + 1)) +
                                     s16((uint32_t)(d1 + sy - ((s16((uint32_t)d0) + a1) >> 1)))));
    if (x > STAR_MIN && x < STAR_X_MAX && y > STAR_MIN && y < STAR_Y_MAX) {
      const uint32_t e = n < STAR_SLOTS ? a2 + STARS + 6 * n : a2 + MASKS + 6 * (n - STAR_SLOTS);
      n++;
      m.ww(e, STAR_CODE + k);
      m.ww(e + 2, (uint16_t)x);
      m.ww(e + 4, (uint16_t)y);
    }
    d3 += STAR_STEP;
    if (s16(d3) >= FULL_TURN) { d3 -= FULL_TURN; }
  }
  m.wb(a2 + VISIBLE, 1);
  return m.rb(a2 + DAMAGE) == 0;
}

// 0x2574c tail: dizzy countdown, back to ROLL at 0 (sound 0x15884; dots 0x1c014 not ported)
void countdown_2574c(Mem &m, uint32_t a2) {
  if (m.rb(a2 + STATE) != DIZZY) { return; }
  if (m.rb(a2 + DAMAGE)) { m.wb(a2 + DAMAGE, m.rb(a2 + DAMAGE) - 1); }
  if (m.sb(a2 + DAMAGE) >= m.sb(a2 + DIZZY_CNT)) {
    m.wb(a2 + DIZZY_CNT, m.rb(a2 + DIZZY_CNT) + 1);
    return;
  }
  m.wb(a2 + DIZZY_CNT, m.rb(a2 + DIZZY_CNT) - 1);
  if (m.rb(a2 + DIZZY_CNT)) { return; }
  m.wb(a2 + STATE, ROLL);
  m.wl(a2 + ANIM, 0);
}

// 0x256d2: roll; a pending time bonus (+0xcb, 0x1bbaa) on the ground starts it
// (disassembly 0x256d2-0x25730)
void roll_256d2(Mem &m, uint32_t a2) {
  const uint8_t s = m.rb(a2 + SURFACE);
  const bool plain = s == 0 || s == BONUS_SURFACE_A || s == BONUS_SURFACE_B;
  if (m.rb(a2 + BONUS_EVENT) && m.rb(a2 + AIR) == ON_GROUND && plain) {
    m.wb(a2 + DIZZY_CNT, BONUS_RATE);
    m.wb(a2 + DAMAGE, BONUS_SECONDS);
    m.wb(a2 + BONUS_EVENT, 0);
    m.wb(a2 + STATE, TIME_BONUS);
    marble_sound::send(m, marble_sound::SND_TIME_BONUS);
    m.wl(a2 + BONUS_ANIM, ANIM_BONUS);
    m.wb(a2 + BONUS_TICK, 0);
    m.wb(a2 + BONUS_ON, 1);
    return;
  }
  ground_253bc(m, a2);
  input_17f66(m, a2);
  physics_121b8(m, a2);
}

// 0x2574c: dizzy, the roll with the animation and the countdown
void dizzy_2574c(Mem &m, uint32_t a2) {
  anim_25fc2(m, a2);
  roll_256d2(m, a2);
  countdown_2574c(m, a2);
}

// 0x25824: shatter animation, projection, masks while visible
void shatter_25824(Mem &m, uint32_t a2) {
  anim_25fc2(m, a2);
  proj_1b9cc(m, a2, REFETCH);
  if (m.rb(a2 + VISIBLE)) { marble_occlusion::enter(m, a2); }
}

// 0x255c6: lost, the sparkle at the respawn point; once it ends the spot is checked again
// (taken: respawn elsewhere), then ROLL
void lost_255c6(Mem &m, uint32_t a2) {
  proj_1b9cc(m, a2, REFETCH);
  bool done = orbit_13ade(m, a2);
  const bool check = done || m.sb(a2 + DAMAGE) < SPARKLE_LAST;
  if (check && blocked_17cb8(m, a2, m.sw(a2 + PX), m.sw(a2 + PY), RESPAWN_FREE)) {
    m.wb(a2 + DAMAGE, EMBEDDED);
    enter_25bae(m, a2, LOST);
    done = false;
  }
  if (!done) { return; }

  m.wl(a2 + VY, 0);
  m.wl(a2 + VX, 0);
  m.wb(a2 + STATE, ROLL);
}

// 0x258a8: time bonus, +1 s every +0x56 ticks, +0x57 times (the race timer pauses), then
// ROLL with 0x285b0(0x10); +0xcc steps every 2 ticks. Dots 0x1c014 not ported.
void bonus_258a8(Mem &m, uint32_t a2) {
  if (m.rb(a2 + DIZZY_CNT)) { m.wb(a2 + DIZZY_CNT, m.rb(a2 + DIZZY_CNT) - 1); }
  if (!m.rb(a2 + DIZZY_CNT)) {
    m.wb(a2 + DIZZY_CNT, BONUS_RATE);
    m.ww(a2 + TIMER, m.rw(a2 + TIMER) + 1);
    m.wb(a2 + DAMAGE, m.rb(a2 + DAMAGE) - 1);
  }

  if (!m.rb(a2 + DAMAGE)) {
    m.wb(a2 + BONUS_ON, 0);
    m.wb(a2 + STATE, ROLL);
    score(m, a2, SCORE_BONUS_END);
  } else {
    m.wb(a2 + BONUS_TICK, m.rb(a2 + BONUS_TICK) + 1);
    if (m.rb(a2 + BONUS_TICK) == ANIM_RATE) {
      m.wb(a2 + BONUS_TICK, 0);
      m.wl(a2 + BONUS_ANIM, m.rl(a2 + BONUS_ANIM) + 4);
    }
  }
  proj_1b9cc(m, a2, REFETCH);
  marble_occlusion::enter(m, a2);
}

// 0x254d2: goal, physics until +0x57 counts to 0, then out of play; the attract demo
// steers on (0x1815a)
void goal_254d2(Mem &m, uint32_t a2) {
  if (inAttract(m)) { waypoint_1815a(m, a2); }
  physics_121b8(m, a2);
  m.wb(a2 + DAMAGE, m.rb(a2 + DAMAGE) - 1);
  if (m.rb(a2 + DAMAGE) || m.rb(a2 + SLOT) == OUT_OF_LIST) { return; }

  m.wb(a2 + SLOT, OUT_OF_PLAY);
  m.wl(a2 + VY, 0);
  m.wl(a2 + VX, 0);
}

// 0x257ba: held by a slinky (0x14e92 set +0x56 = 50), the roll with the eaten animation;
// back to ROLL when +0x56 runs out (sound 0x15884; dots 0x1c014 not ported)
void eaten_257ba(Mem &m, uint32_t a2) {
  anim_25fc2(m, a2);
  roll_256d2(m, a2);
  if (m.rb(a2 + STATE) != EATEN) { return; }

  if (m.rb(a2 + DIZZY_CNT)) { m.wb(a2 + DIZZY_CNT, m.rb(a2 + DIZZY_CNT) - 1); }
  if (m.rb(a2 + DIZZY_CNT)) { return; }
  m.wb(a2 + STATE, ROLL);
  m.wl(a2 + ANIM, 0);
}

// 0x25812: inside a slinky, hidden until the slinky sends it LOST
void hidden_25812(Mem &m, uint32_t a2) {
  ground_253bc(m, a2);
  m.wb(a2 + VISIBLE, 0);
}

// 0x2584e: dissolving in acid: slides into it (0x176d2), dissolve animation (list end: LOST),
// projection, masks
void dissolve_2584e(Mem &m, uint32_t a2) {
  marble_enemies::target_176d2(m, a2);
  anim_25fc2(m, a2);
  proj_1b9cc(m, a2, REFETCH);
  marble_occlusion::enter(m, a2);
}

// 0x25514: dropped out of a tube exit (+0x58 the tube tag): projection, visibility (dots
// 0x1c014 not ported); +0x59 counts down, then points (0x12: 9; 0x13, 0x14, 0x20, 0x22,
// 0x25: 5), surface and the older screen pairs +0x22/+0x26 cleared, ROLL
void tube_25514(Mem &m, uint32_t a2) {
  static const uint8_t TUBE_12 = 0x12, POINTS_12 = 9, POINTS_TUBE = 5, CATAPULT = 0x0a;
  proj_1b9cc(m, a2, REFETCH);
  marble_occlusion::enter(m, a2);
  const uint8_t t = m.rb(a2 + SURFACE);
  if (!t) { return; }

  m.wb(a2 + SURFACE_T, m.rb(a2 + SURFACE_T) - 1);
  if (m.rb(a2 + SURFACE_T)) { return; }

  if (t == TUBE_12) {
    score(m, a2, POINTS_12);
  } else if (t == 0x13 || t == 0x14 || t == 0x20 || t == 0x22 || t == 0x25) {
    score(m, a2, POINTS_TUBE);
  } else if (t == CATAPULT) {
    marble_sound::send(m, marble_sound::SND_FALL);
  }
  m.wb(a2 + SURFACE, 0);
  m.wl(a2 + SCREEN_OLD2, 0);
  m.wl(a2 + SCREEN_OLD, 0);
  m.wb(a2 + STATE, ROLL);
}

// 0x25876: carried off by a bird: projection, orbit (radius grows), at its end LOST
void carried_25876(Mem &m, uint32_t a2) {
  proj_1b9cc(m, a2, REFETCH);
  if (!orbit_13ade(m, a2)) { return; }
  m.wb(a2 + DAMAGE, EMBEDDED);
  enter_25bae(m, a2, LOST);
}

// 0x13d38: 8 stars (code STAR_CODE + k) on the way from the screen position to the gate's
// mouth and on into the gate (slot +0x4e, offset by the gate kind), moving one star step per
// 2 ticks; first 4 to +0xa4, next 4 to +0x38. +0x57 counts down; true when it reaches 0.
bool suck_13d38(Mem &m, uint32_t a2) {
  static const uint32_t SLOT_LIST = 0x1f016;  // slot addresses by number (+0x58)
  static const uint8_t G_TAG = 0x1f, G_SCREEN = 0x4e, GATE_D = 0x0d;
  static const int16_t D_X = -5, B_X = 3, G_Y = 5, MOUTH_X = 0x1c, MOUTH_Y = -0xe;
  static const int16_t STEPS_IN = 8, MOUTH_STEP = 4;  // star step: out of sight, at the mouth
  const int16_t d2 = (int16_t)(0x20 - m.sb(a2 + DAMAGE));
  m.wb(a2 + DAMAGE, m.rb(a2 + DAMAGE) - 1);

  const uint32_t g = m.roml(SLOT_LIST + 4 * m.sb(a2 + SURFACE));
  const bool d = m.rb(g + G_TAG) == GATE_D;
  const int16_t gx = (int16_t)(m.sw(g + G_SCREEN) + (d ? D_X : B_X));
  const int16_t gy = (int16_t)(m.sw(g + G_SCREEN + 2) + G_Y);
  const int16_t ex = (int16_t)(gx + (d ? -MOUTH_X : MOUTH_X)), ey = (int16_t)(gy + MOUTH_Y);
  const int16_t mx = m.sw(a2 + SCREEN), my = m.sw(a2 + SCREEN + 2);
  const int16_t ax = (int16_t)(ex - mx), ay = (int16_t)(ey - my);  // marble to the mouth
  const int16_t bx = (int16_t)(gx - ex), by = (int16_t)(gy - ey);  // mouth to the gate
  for (uint8_t k = 0; k < STAR_SLOTS; k++) {
    m.ww(a2 + MASKS + 6 * k, 0);
    m.ww(a2 + STARS + 6 * k, 0);
  }

  uint8_t n = 0;
  for (uint8_t k = 0; k < STAR_N; k++) {
    const int16_t d1 = (int16_t)(d2 - 2 * k);
    if (d1 >= STEPS_IN) { continue; }

    int16_t x = mx, y = my;
    if (d1 >= MOUTH_STEP) {
      x = (int16_t)((((int32_t)(d1 - MOUTH_STEP) * bx) >> 2) + ex);
      y = (int16_t)((((int32_t)(d1 - MOUTH_STEP) * by) >> 2) + ey);
    } else if (d1 >= 0) {
      x = (int16_t)((((int32_t)d1 * ax) >> 2) + mx);
      y = (int16_t)((((int32_t)d1 * ay) >> 2) + my);
    }
    const uint32_t e = n < STAR_SLOTS ? a2 + STARS + 6 * n : a2 + MASKS + 6 * (n - STAR_SLOTS);
    n++;
    m.ww(e, STAR_CODE + k);
    m.ww(e + 2, (uint16_t)(s8(m.romb(STAR_STEPS + 2 * k)) + x));
    m.ww(e + 4, (uint16_t)(s8(m.romb(STAR_STEPS + 2 * k + 1)) + y));
  }
  m.wb(a2 + VISIBLE, 1);
  return m.rb(a2 + DAMAGE) == 0;
}

// 0x2563e: sucked into an aerial gate: projection, stars; then the gate closes (sound 0x3c),
// death count + 1, LOST where it was caught (no 0x25bae: no new respawn point)
void caught_2563e(Mem &m, uint32_t a2) {
  proj_1b9cc(m, a2, REFETCH);
  if (!suck_13d38(m, a2)) { return; }

  marble_objects::closeGate(m, m.rb(a2 + SURFACE));
  m.wl(a2 + ANIM, 0);
  m.wb(a2 + STATE, LOST);
  m.ww(a2 + DEATHS, m.rw(a2 + DEATHS) + 1);
  m.wb(a2 + DAMAGE, EMBEDDED);
}

// 0x25416: while a score shows (+0xd8, 0x285b0) +0x68 swings -0x16..2 by +0x69, odd values
// count +0x70 up to 0x28, then it ends (silly mini squash too)
void wobble_25416(Mem &m, uint32_t a2) {
  m.wb(a2 + SCORE_SHOWN, m.rb(a2 + SCORE_SHOWN) + m.rb(a2 + SCORE_FLAG));
  if (m.sb(a2 + SCORE_SHOWN) > WOBBLE_HI) {
    m.wb(a2 + SCORE_SHOWN, WOBBLE_HI);
    m.wb(a2 + SCORE_FLAG, 0xff);
  } else if (m.sb(a2 + SCORE_SHOWN) < WOBBLE_LO) {
    m.wb(a2 + SCORE_SHOWN, (uint8_t)WOBBLE_LO);
    m.wb(a2 + SCORE_FLAG, 1);
  }
  if (!(m.rb(a2 + SCORE_SHOWN) & 1)) { return; }

  m.wb(a2 + SCORE_TEXT_ON, m.rb(a2 + SCORE_TEXT_ON) + 1);
  if (m.sb(a2 + SCORE_TEXT_ON) <= WOBBLE_STEPS) { return; }
  m.wb(a2 + SCORE_TEXT_ON, 0xff);
  m.wb(a2 + SCORE_WAIT, 0);
}

// 0x253ec: handler of state +0x1a (table 0x254ba), after the score wobble 0x25416
void object_253ec(Mem &m, uint32_t a2) {
  const uint8_t st = m.rb(a2 + STATE);
  const bool still = st == BROKEN || st == LOST || st == CAUGHT || st == CARRIED || st == HIDDEN;
  if (m.rb(a2 + SCORE_WAIT) && !still) { wobble_25416(m, a2); }

  switch (st) {
    case ROLL: roll_256d2(m, a2); break;
    case DIZZY: dizzy_2574c(m, a2); break;
    case BROKEN: shatter_25824(m, a2); break;
    case TUBE: tube_25514(m, a2); break;
    case LOST: lost_255c6(m, a2); break;
    case EATEN: eaten_257ba(m, a2); break;
    case GOAL: goal_254d2(m, a2); break;
    case HIDDEN: hidden_25812(m, a2); break;
    case TIME_BONUS: bonus_258a8(m, a2); break;
    case DISSOLVING: dissolve_2584e(m, a2); break;
    case CARRIED: carried_25876(m, a2); break;
    case CAUGHT: caught_2563e(m, a2); break;
    default: break;
  }
}

// 0x1bbaa: armed, race start random byte below the race's limit, the player rolling with 3-6
// s left in a zone of the race's list: time bonus pending (+0xcb of players in play), disarmed
void bonusEvent_1bbaa(Mem &m) {
  const uint16_t race = m.rw(RACE);
  if (!m.rb(TIMEOUT_ARMED) || m.romb(BONUS_LIMIT + race) <= m.rb(TIMEOUT_RND)) { return; }
  const uint32_t zones = m.roml(BONUS_ZONES + 4 * race);
  const uint32_t a2 = PLAYER;
  bool listed = false;
  for (uint32_t z = zones; m.romb(z) != 0xff; z++) { listed |= m.rb(a2 + ZONE) == m.romb(z); }

  const int16_t s = m.sw(a2 + TIMER);
  if (!listed || m.rb(a2 + SLOT) != 1 || m.rb(a2 + STATE) != ROLL || s <= 2 || s >= 7) { return; }
  m.wb(TIMEOUT_ARMED, 0);
  m.wb(a2 + BONUS_EVENT, 1);
}

}  // namespace

void startRace(Mem &m, uint8_t race) {
  m.setRace(race);
  start_259b4(m, race, PLAYER);
  marble_drawlist::init(m);  // 0x28ca6: before the slot inserts of the level init
  marble_enemies::init(m);
  marble_objects::init(m, race);

  // 0x10504 end: nobody at the goal; 0x10fb8: time bonus event armed, its random byte
  m.wb(GOAL_FIRST, 0xff);
  m.wb(TIMEOUT_ARMED, 1);
  m.wb(TIMEOUT_RND, (uint8_t)marble_rng::rng(m, RNG_BYTE));
}

// 0x251de, one player: time bonus event 0x1bbaa, the state handler; out of play after the
// goal ends the race (0x400390 = 3, not in the attract demo)
void tick(Mem &m, int8_t c6, int8_t c7) {
  const uint32_t a2 = PLAYER;
  m.wb(a2 + BALL_C6, (uint8_t)c6);
  m.wb(a2 + BALL_C7, (uint8_t)c7);
  bonusEvent_1bbaa(m);
  if (!m.rb(a2 + SLOT)) { return; }

  object_253ec(m, a2);
  if (m.rb(a2 + SLOT) == OUT_OF_PLAY && m.rw(GAME_STATE) != marble_flow::ATTRACT) {
    m.ww(GAME_STATE, marble_flow::RACE_END);
  }
}

void project(Mem &m) { proj_1b9cc(m, PLAYER, KEEP_CELL); }
void project(Mem &m, uint32_t obj, Refetch r) { proj_1b9cc(m, obj, r); }

bool isPlayer(uint32_t obj) { return obj == PLAYER || obj == PLAYER + PLAYER_SIZE; }
void move(Mem &m, uint32_t obj) { physics_121b8(m, obj); }
void ground(Mem &m, uint32_t obj) { ground_253bc(m, obj); }
void speedCap(Mem &m, uint32_t obj) { speedcap_26196(m, obj); }
void enterState(Mem &m, uint32_t obj, State s) { enter_25bae(m, obj, s); }
void land(Mem &m, uint32_t player, int16_t fall) { land_25c74(m, player, fall); }
uint8_t blocked(Mem &m, uint32_t obj, int16_t x, int16_t y, uint16_t r) {
  return blocked_17cb8(m, obj, x, y, r);
}
void animate(Mem &m, uint32_t obj) { anim_25fc2(m, obj); }

void cellScratch(Mem &m, int16_t x, int16_t y) {
  m.ww(POS_X, (uint16_t)x);
  m.ww(POS_Y, (uint16_t)y);
  derive_1bb50(m);
}

int32_t cellHeight(Mem &m, uint32_t obj) {
  m.ww(POS_X, m.rw(obj + PX));
  m.ww(POS_Y, m.rw(obj + PY));
  derive_1bb50(m);
  terrain_1caba(m);
  return height_1cc62(m);
}

void drag(Mem &m, uint32_t obj) { friction_25e7c(m, obj, FR_2); }

int32_t floorZ(Mem &m, uint32_t obj) {
  m.ww(CELL_Y, 0xffff);
  m.ww(CELL_X, 0xffff);
  posupd_1bab2(m, obj);
  return height_1cc62(m);
}

// 0x285b0 (helper-285b0.ts): points 0x23cd4[k] via 0x28608 (+0xbc, flag 0x40039c), score
// text 0x23cf6[k] shown (+0xd4, +0x68/+0x69/+0x70/+0xd8)
void score(Mem &m, uint32_t player, uint8_t k) {
  m.wl(player + SCORE, m.rl(player + SCORE) + (uint32_t)(int32_t)s16(m.romw(SCORES + 2 * k)));
  const uint8_t bit = m.rb(player + PLAYER_NO) & 0x3f;
  if (bit < 8) { m.wb(SCORED, m.rb(SCORED) | (uint8_t)(1u << bit)); }
  m.wl(player + SCORE_TEXT, m.roml(SCORE_TEXTS + 4 * k));
  m.wb(player + SCORE_TEXT_ON, 0);
  m.wb(player + SCORE_SHOWN, 0);
  m.wb(player + SCORE_FLAG, 0xff);
  m.wb(player + SCORE_WAIT, 1);
}

}  // namespace marble_physics

#endif  // ENABLE_MARBLE
