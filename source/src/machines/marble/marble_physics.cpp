#include "marble_physics.h"

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
  STATE = 0x1a,                     // enum State
  ZONE = 0x1b, GROUND_ZONE = 0x1d,
  GROUND_Z = 0x2a,                  // z at last ground (fall height)
  EDGE_CX = 0x2e, EDGE_CY = 0x30,   // cell where teetering / the fall started
  GROUND_CX = 0x32, GROUND_CY = 0x34,
  AIR = 0x36,                       // 0 ground, 1 teetering, 2 airborne
  EDGE_MASK = 0x37,
  DIZZY_CNT = 0x56, DAMAGE = 0x57,
  SURFACE = 0x58, SURFACE_T = 0x59,
  ANIM = 0x5a, ANIM_5F = 0x5f, ANIM_60 = 0x60,
  BALL_C6 = 0xc6, BALL_C7 = 0xc7,   // trackball deltas port 2 / port 0
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
static const uint32_t ATTRACT = 0x400390;                    // 1 = attract demo
static const uint32_t RACE = 0x400394;
static const uint32_t DYN = 0x40076e;                        // dynamic terrain codes
static const uint32_t SPAWN_X = 0x400462, SPAWN_Y = 0x400466;  // longs, respawn pixel
static const uint32_t SPAWN_ZONE = 0x400472;                 // 0xff: none found
static const uint32_t VERTS = 0x401c28;                      // 4 vertices x 4 heights

// program ROM tables
static const uint32_t DYN_INTERMEDIATE = 0x1d370;  // 66 dynamic codes (0x11ac2)
static const uint32_t ZONE_LISTS = 0x1eb16;        // zone line list per race
static const uint32_t GOAL_VX = 0x1eb2e, GOAL_VY = 0x1eb34;
static const uint32_t CHECKPOINTS = 0x1ef1a;       // list per race: cx, cy, zone, pad; 0xff end
static const uint32_t DROP = 0x1ed62;              // two level code height steps
static const uint32_t FRICTION = 0x1eef8;
static const uint32_t TEETER_LIFT = 0x2398c;   // z lift by distance to the edge
static const uint32_t PUSH = 0x23d40;
static const uint32_t STEPS = 0x24b3a;             // vertex walk of 0x1caba

static const uint32_t FALL_VZ = 0xffffa000;        // -0x6000: vz when a fall starts
static const uint8_t RACE_PRACTICE = 0, RACE_INTERMEDIATE = 2, RACE_AERIAL = 3, RACE_SILLY = 4;
static const uint8_t DYN_PRACTICE = 9, DYN_INTERMEDIATE_N = 0x42;
static const uint16_t SPAWN_RANGE = 0x300;         // max checkpoint distance (max + 3/8 min) * 16
static const uint8_t NO_ZONE = 0xff;

inline int8_t s8(int32_t v) { return (int8_t)v; }
inline int16_t s16(int32_t v) { return (int16_t)v; }
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
      if (s32((int64_t)m.sl(a2 + PZ) - h) > 0x60000) { startFall(m, a2); }  // sound 0x45
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

    const int8_t mag = s8(m.romb(TEETER_LIFT + ti));
    if (best == -1 || mag > best) {
      best = mag;
      bestv = cur;
    }
  }

  if (best == -1) {
    m.wb(a2 + AIR, AIRBORNE);  // sound 0x45
    m.wl(a2 + VZ, FALL_VZ);
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

// 0x29cce: level object slots (Phase 4). Without slots it only clears the surface code.
void slots_29cce(Mem &m, uint32_t a2) { m.wb(a2 + SURFACE, 0); }

// 0x25c74: landing (player path)
void land_25c74(Mem &m, uint32_t a2, int16_t fall) {
  const uint8_t prev = m.rb(a2 + STATE);
  m.wb(a2 + STATE, DIZZY);
  int32_t d4 = s8(m.rb(a2 + DAMAGE)) + fall;
  if (d4 > 0x7f) { d4 = 0x7f; }
  m.wb(a2 + DAMAGE, (uint8_t)d4);

  const int8_t s56 = m.sb(a2 + DIZZY_CNT), s57 = m.sb(a2 + DAMAGE);
  if (s56 + ((s57 - s56) >> 1) > 0x1f) {
    m.wb(a2 + STATE, BROKEN);
    return;
  }
  if (prev == DIZZY) { return; }
  m.wb(a2 + ANIM_5F, 0);
  m.wb(a2 + ANIM_60, 2);
  m.wl(a2 + ANIM, 0x20faa);  // sound 0x39
}

// 0x1365c: zone lines (checkpoint zone +0x1b, goal zone 0xff)
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
          m.wb(a2 + STATE, GOAL);
          m.wb(a2 + DAMAGE, 0x1e);
          if (race == RACE_PRACTICE && (d2 == 3 || d2 == 4)) {
            m.wl(a2 + VX, 0);
            m.wl(a2 + VY, 0);
          } else {
            m.wl(a2 + VX, (uint32_t)(int32_t)s8(m.romb(GOAL_VX + race)) << 16);
            m.wl(a2 + VY, (uint32_t)(int32_t)s8(m.romb(GOAL_VY + race)) << 16);
          }
        }
        return;
      }
    }
    if (m.romb(a0 + 3) == 0xff && m.romb(a0 + 4) == 0xff) { return; }
  }
}

// 0x121b8: object physics update (player object only)
void physics_121b8(Mem &m, uint32_t a2) {
  m.wl(OLD_X, m.rl(a2 + PX));
  m.wl(OLD_Y, m.rl(a2 + PY));
  m.wl(OLD_Z, m.rl(a2 + PZ));
  m.ww(OLD_CX, (uint16_t)(m.sl(OLD_X) >> 19));
  m.ww(OLD_CY, (uint16_t)(m.sl(OLD_Y) >> 19));
  m.ww(CELL_Y, 0xffff);
  m.ww(CELL_X, 0xffff);
  posupd_1bab2(m, a2);

  // more than 16 px below the terrain: embedded, lost
  if (s32((int64_t)height_1cc62(m) - m.sl(a2 + PZ)) > 0x100000) {
    m.wb(a2 + DAMAGE, 0x65);
    m.wb(a2 + STATE, LOST);
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
  slots_29cce(m, a2);
  if (m.rb(a2 + SURFACE) == 0xa || m.rb(a2 + STATE) == LOST) { return; }

  // 0x1bc88 object pair collision and enemy updates: not ported
  zones_1365c(m, a2);
  if (m.rb(a2 + STATE) == 0xb) { return; }

  const int32_t h = height_1cc62(m);
  edge_160f6(m, a2, h);

  // z changed more than 8 px on a flat surface code: start a fall (sound 0x45)
  const int64_t dz = abs64(s32((int64_t)m.sl(a2 + PZ) - m.sl(OLD_Z)));
  if (dz > 0x80000 && m.rb(a2 + SURFACE) == 0 && m.sl(a2 + VZ) <= 0) { startFall(m, a2); }

  if (m.rb(a2 + AIR) == AIRBORNE) {
    if (h >= m.sl(a2 + PZ)) {
      m.wb(a2 + AIR, ON_GROUND);
      m.wl(a2 + VZ, 0);
      const int64_t fall = abs64(s32((int64_t)m.sl(a2 + GROUND_Z) - h)) >> 15;
      m.wl(a2 + PZ, (uint32_t)h);
      land_25c74(m, a2, s16((int32_t)fall));
    } else if (s32((int64_t)m.sl(a2 + GROUND_Z) - m.sl(a2 + PZ)) > 0x800000) {
      m.wb(a2 + DAMAGE, 0x64);  // fell more than 128 px: lost
      m.wb(a2 + STATE, LOST);
      return;
    }
  } else if (m.rb(a2 + AIR) == ON_GROUND) {
    m.wl(a2 + PZ, (uint32_t)h);
  }

  // 0x1b9cc / 0x1c014 / 0x1281c sprite: not ported
  edgepush_1706c(m, a2);
  if (m.rb(a2 + AIR)) { return; }

  const Friction fm = frictionMode(m.rb(a2 + SURFACE));
  if (fm != FR_NONE) { friction_25e7c(m, a2, fm); }
  if (m.rw(ATTRACT) != 1) { gravity_25df6(m, a2); }
}

// 0x26196 / 0x261bc: speed cap
void speedcap_26196(Mem &m, uint32_t a2) {
  const uint32_t cap = m.rb(a2 + STATE) == ROLL ? 0x40000 : 0x50000;
  const uint32_t d3 = speedNorm(m.sl(a2 + VX), m.sl(a2 + VY));
  if (cap >= d3) { return; }

  const uint16_t q = divu(cap << 6, (d3 >> 8) & 0xffff);
  m.wl(a2 + VX, scaleV(m.sl(a2 + VX), q));
  m.wl(a2 + VY, scaleV(m.sl(a2 + VY), q));
}

// surface codes where 0x17f66 lets the trackball steer
bool steerSurface(uint8_t c) {
  return c == 0 || c == 0x3b || c == 0x2d || c == 0x2e || (c >= 0x38 && c <= 0x3a) ||
         (c >= 0x2f && c <= 0x31);
}

// 0x17f66: input -> velocity
void input_17f66(Mem &m, uint32_t a2) {
  const uint8_t slot = m.rb(a2 + SLOT);
  if (slot == 2 || slot == 3) { return; }

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
// 66 words from 0x1d370. Practice: a level object animation (0x1344c, Phase 4) writes its
// 9 codes; held at the first frame, flat 0xf040.
void dyn_init(Mem &m, uint8_t race) {
  if (race == RACE_INTERMEDIATE) {
    for (uint8_t k = 0; k < DYN_INTERMEDIATE_N; k++) {
      m.ww(DYN + 2 * k, m.romw(DYN_INTERMEDIATE + 2 * k));
    }
  } else if (race == RACE_PRACTICE) {
    for (uint8_t k = 0; k < DYN_PRACTICE; k++) { m.ww(DYN + 2 * k, 0xf040); }
  }
}

// 0x259b4: race start of player 1, physics fields only (sprite setup skipped)
void start_259b4(Mem &m, uint8_t race, uint32_t a2) {
  m.ww(RACE, race);
  dyn_init(m, race);

  // start cell: high byte x, low byte y; position = cell * 8 + 4
  const uint16_t cell = m.romw(m.desc() + 0x14);
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
  if (race == RACE_AERIAL) {  // starts on surface 0x17
    m.wb(a2 + SURFACE, 0x17);
    m.wb(a2 + SURFACE_T, 0xff);
  }

  // first object update before the first 0x121b8 (deltas are 0)
  ground_253bc(m, a2);
  input_17f66(m, a2);
}

// checkpoint entry a3 -> spawn pixel (cell * 8 + 4) and zone
void spawnAt(Mem &m, uint32_t a3) {
  m.wl(SPAWN_X, (uint32_t)(m.romb(a3) * 8 + 4));
  m.wl(SPAWN_Y, (uint32_t)(m.romb(a3 + 1) * 8 + 4));
  m.wb(SPAWN_ZONE, m.romb(a3 + 2));
}

// 0x2637a: nearest checkpoint of the ground zone (+0x1d) to the last ground cell
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

    // 0x17cb8 (spot blocked by another object): not ported, no objects yet
    if (d >= best) { continue; }
    best = d;
    spawnAt(m, a3);
  }
}

// 0x262b2: respawn point; fallback is the last checkpoint of zone - 1
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

  // backwards from the end marker (its zone 0xff never matches)
  for (; a3 != first - 4; a3 -= 4) {
    if (m.romb(a3 + 2) == (uint16_t)zone) {
      spawnAt(m, a3);
      return;
    }
  }
}

}  // namespace

// 0x2591a (physics fields) after a death, then the state 0 entry as in start_259b4.
// Placeholder for the state 2/4 sequence (0x25c34, 0x13ade): state back to 0, damage 0.
void respawn(Mem &m) {
  const uint32_t a2 = PLAYER;
  spawnPoint_262b2(m, a2);
  if (m.rb(SPAWN_ZONE) == NO_ZONE) {  // the original would use pixel (0, 0): off the terrain
    const uint16_t cell = m.romw(m.desc() + 0x14);
    m.wl(SPAWN_X, (cell >> 8) * 8 + 4);
    m.wl(SPAWN_Y, (cell & 0xff) * 8 + 4);
    m.wb(SPAWN_ZONE, 0);
  }

  m.wl(a2 + PX, m.rl(SPAWN_X) << 16);
  m.wl(a2 + PY, m.rl(SPAWN_Y) << 16);
  m.ww(CELL_Y, 0xffff);
  m.ww(CELL_X, 0xffff);
  posupd_1bab2(m, a2);
  m.wl(a2 + PZ, (uint32_t)height_1cc62(m));
  m.wb(a2 + ZONE, m.rb(SPAWN_ZONE));

  static const uint8_t longs[] = {VX, VY, VZ, ANIM};
  static const uint8_t bytes[] = {DIZZY_CNT, DAMAGE, STATE, AIR, SURFACE, BALL_C6, BALL_C7};
  for (uint8_t o : longs) { m.wl(a2 + o, 0); }
  for (uint8_t o : bytes) { m.wb(a2 + o, 0); }

  ground_253bc(m, a2);
  input_17f66(m, a2);
}

void startRace(Mem &m, uint8_t race) {
  m.setRace(race);
  start_259b4(m, race, PLAYER);
}

// Snapshot point (0x121b8 entry) to the next one; c6/c7: trackball deltas of the next tick.
void tick(Mem &m, int8_t c6, int8_t c7) {
  const uint32_t a2 = PLAYER;
  const uint8_t st = m.rb(a2 + STATE);  // the state handler was picked before the physics
  physics_121b8(m, a2);

  // 0x2574c tail: dizzy countdown
  if (st == DIZZY && m.rb(a2 + STATE) == DIZZY) {
    if (m.rb(a2 + DAMAGE)) { m.wb(a2 + DAMAGE, m.rb(a2 + DAMAGE) - 1); }
    if (m.sb(a2 + DAMAGE) < m.sb(a2 + DIZZY_CNT)) {
      m.wb(a2 + DIZZY_CNT, m.rb(a2 + DIZZY_CNT) - 1);
      if (m.rb(a2 + DIZZY_CNT) == 0) {
        m.wb(a2 + STATE, ROLL);
        m.wl(a2 + ANIM, 0);
      }
    } else {
      m.wb(a2 + DIZZY_CNT, m.rb(a2 + DIZZY_CNT) + 1);
    }
  }

  m.wb(a2 + BALL_C6, (uint8_t)c6);
  m.wb(a2 + BALL_C7, (uint8_t)c7);
  const uint8_t now = m.rb(a2 + STATE);
  if (now != ROLL && now != DIZZY) { return; }

  // 0x25fc2 animation script step (state 1) does not touch the motion
  ground_253bc(m, a2);
  input_17f66(m, a2);
}

}  // namespace marble_physics

#endif  // ENABLE_MARBLE
