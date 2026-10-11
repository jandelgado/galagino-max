#include "marble_objects.h"

#ifdef ENABLE_MARBLE

#include "marble_drawlist.h"
#include "marble_physics.h"

// 1:1 port of the level object part of tools/marble_sim.py (function names and order as
// there). Integer semantics of the 68010: word results wrap at 16 bits, long results at 32
// bits (done in uint32_t), >> on signed values is asr.

namespace marble_objects {

using marble_physics::Mem;
using marble_physics::PLAYER;
using marble_physics::RACE;
using marble_physics::ULT_COL;
using marble_physics::DYN;
using marble_physics::CAMERA;
using marble_physics::CAMERA_L;
using marble_physics::DESC_TOP;
using marble_physics::DESC_RANGE;
using marble_physics::s8;
using marble_physics::s16;

namespace {

inline int32_t s32(uint32_t v) { return (int32_t)v; }

// level object slots
static const uint32_t SLOTS = 0x400a9c;
static const uint8_t SLOT_N = 25;
static const uint8_t SLOT_SIZE = 0x56;
static const uint32_t PLAYER_SIZE = 0xe2;  // player objects are 0xe2 apart

// slot (object) fields
enum : uint8_t {
  VX = 0x00, VY = 0x04,              // velocity, 16.16
  PX = 0x0c, PY = 0x10, PZ = 0x14,   // position
  ACTIVE = 0x18,                     // 1 in use
  IDX = 0x19,                        // slot number (draw list index)
  STATE = 0x1a,                      // 0 wait, 1/2 animate, 3 start, 4 zone wait
  ZONE = 0x1b,
  COUNT = 0x1c,                      // word: ticks / animation loops
  MODE = 0x1e,                       // 0 MO, 1 playfield patch, 2 MO + terrain
  TAG = 0x1f,
  DIV1 = 0x20, PERIOD1 = 0x21,       // state 1 tick divider
  DIV2 = 0x22, PERIOD2 = 0x23,       // state 2 tick divider
  LOOP5 = 0x24, LOOP7 = 0x25,        // loop counters
  PATCH_COL = 0x26, PATCH_ROW = 0x28,
  LOOP5_PC = 0x2a, LOOP7_PC = 0x2e,
  RET_PC = 0x32, PC = 0x36, SCRIPT = 0x3a,
  ANIM = 0x3e, FRAME = 0x42,         // current animation frame, MO frame
  ANIM2 = 0x46, ANIM1 = 0x4a,        // animation start for state 2 / 1
  SCREEN = 0x4e,                     // screen position sx << 16 | sy
  BAND_LO = 0x52, BAND_HI = 0x54,    // camera band range of the slot
};

// player fields used here
static const uint8_t P_VX = 0x00, P_VY = 0x04, P_PZ = 0x14, P_SURFACE = 0x58, P_SURFACE_T = 0x59;

// work RAM globals
static const uint32_t HIT_X = 0x400666, HIT_Y = 0x400668;  // wall bounce flags
static const uint32_t OLD_X = 0x400684, OLD_Y = 0x400688;  // position before the move
static const uint32_t POS_X = 0x400690, POS_Y = 0x400692;
static const uint32_t CELL_X = 0x400696, CELL_Y = 0x400698;
static const uint32_t PLAYERS = 0x400396;
static const uint32_t RNG = 0x4003a6;
static const uint32_t MASK_TAB0 = 0x40044a, MASK_TAB1 = 0x40044e, MASK_TAB2 = 0x400452;
static const uint32_t MASK_T0 = 0x400456, MASK_T1 = 0x400458, MASK_T2 = 0x40045a;
static const uint32_t WAVES = 0x40075c, WAVE_DONE = 0x40075e;
static const uint32_t DYN_PAUSE = 0x400768, DYN_FLAG = 0x40076a;
static const uint32_t ANIM_REQ = 0x400970;     // frame record queued for applyQueued
static const uint32_t CUR_SLOT = 0x400974;     // slot that queued it
static const uint32_t PATCHED = 0x400978;      // record of the last playfield patch

// ROM
static const uint32_t ROM_END = 0x30000;
static const uint32_t ANIM_NONE = 0x20c14;     // empty animation (first long 0xffffffff)
static const uint32_t SLOT_LIST = 0x1f016;     // slot addresses
static const uint32_t SPAWN_LISTS = 0x1dec0;   // per race: entries lo, hi, script (6 bytes)
static const uint32_t SPAWN_GROUPS = 0x1ded8;  // 4 groups of 4 scripts
static const uint32_t WAVE_SCRIPT = 0x1d854;
static const uint32_t DYN_INTERMEDIATE = 0x1d370;
static const uint8_t DYN_INTERMEDIATE_N = 0x42;
static const uint32_t END_MARK = 0xffffffff;

static const uint8_t RACE_INTERMEDIATE = 2, RACE_SILLY = 4;
static const uint8_t TAG_WAVE = 6, TAG_BANNER = 3, TAG_ULTIMATE = 0x19, TAG_CROWD = 0x0c;
static const uint8_t TAG_ROW_LO = 0x17, TAG_ROW_HI = 0x18;
static const uint8_t TAG_POST = 0x05, TAG_BOUNDS_LO = 5, TAG_BOUNDS_HI = 0x3b;
static const uint8_t ST_WAIT = 0, ST_ANIM1 = 1, ST_ANIM2 = 2, ST_START = 3, ST_ZONE = 4;
static const uint8_t MODE_PATCH = 1, MODE_MO_TERRAIN = 2;
static const uint8_t SPAWN_ADD = 0, SPAWN_REMOVE = 1;

// ROM (< 0x30000) or work RAM, as the 68010 sees it
uint16_t gw(const Mem &m, uint32_t a) { return a < ROM_END ? m.romw(a) : m.rw(a); }
uint32_t gl(const Mem &m, uint32_t a) { return a < ROM_END ? m.roml(a) : m.rl(a); }

inline uint32_t slotAddr(uint8_t i) { return SLOTS + (uint32_t)i * SLOT_SIZE; }

// 0x13a98: random 0..n-1, 16 bit shift register, one step per significant bit of n.
// n > 0: n == 0 never ends the loop; the only caller passes 4.
int16_t rng_13a98(Mem &m, uint16_t n) {
  uint16_t d0 = m.rw(RNG);
  uint16_t d1 = n;
  uint8_t k = 0;
  do {
    uint8_t d2 = (uint8_t)((d0 >> 8) ^ d0);
    if (d2 == 0) { d2 = 0x40; }
    d0 = (uint16_t)((d0 << 1) | ((d2 >> 6) & 1));  // asl.b #2 -> X = bit 6, roxl.w
    k++;
    d1 >>= 1;
  } while (d1);
  m.ww(RNG, d0);

  d0 &= (uint16_t)((1u << k) - 1);
  while (!(n > s16(d0))) { d0 = (uint16_t)(d0 - n); }
  return (int16_t)d0;
}

// 0x12f44: mode 0 spawn (script, state 3), mode 1 remove
void slot_start_12f44(Mem &m, uint32_t a, uint8_t mode, uint32_t script = 0) {
  if (mode == SPAWN_ADD) {
    m.wl(a + SCRIPT, script);
    m.wb(a + STATE, ST_START);
    m.wb(a + ACTIVE, 1);
    return;
  }

  if (a == m.rl(CUR_SLOT)) {
    m.wl(PATCHED, 0);
    m.wl(CUR_SLOT, 0);
  }
  m.wb(a + ACTIVE, 0);
  m.wb(a + STATE, 0);
  if (m.rb(a + TAG) == TAG_WAVE) { m.wb(WAVES, m.rb(WAVES) - 1); }
  if (m.rb(a + MODE) != MODE_PATCH) { marble_drawlist::remove(m, a); }  // 0x18f46
}

// 0x12d6e: first free slot, 0 = none
uint32_t slot_free_12d6e(const Mem &m) {
  for (uint8_t i = 0; i < SLOT_N; i++) {
    const uint32_t a = m.roml(SLOT_LIST + 4 * i);
    if (!m.rb(a + ACTIVE)) { return a; }
  }
  return 0;
}

// 0x12dae: spawn entry already alive. An active slot runs its script, or a random group
// entry (script 0) while a tag 0x0c slot is active.
bool spawned_12dae(const Mem &m, uint32_t ent) {
  for (uint8_t i = 0; i < SLOT_N; i++) {
    const uint32_t a = slotAddr(i);
    if (m.rb(a + ACTIVE) != 1) { continue; }
    if (m.roml(ent + 2) == m.rl(a + SCRIPT)) { return true; }
    if (m.roml(ent + 2) == 0 && m.rb(a + TAG) == TAG_CROWD) { return true; }
  }
  return false;
}

// 0x12dfa: camera band change: spawn entries (lo, hi, script) whose band lo or hi is entered
// from outside [lo, hi]; remove slots whose band range is left.
void spawn_12dfa(Mem &m, int16_t oldB, int16_t newB) {
  const int8_t old = s8(oldB), nw = s8(newB);
  uint32_t e = m.roml(SPAWN_LISTS + 4 * m.rw(RACE));
  for (; m.romb(e) != 0xff; e += 6) {
    const int8_t lo = s8(m.romb(e)), hi = s8(m.romb(e + 1));
    if (!(nw == lo || nw == hi) || (lo <= old && old <= hi)) { continue; }
    if (spawned_12dae(m, e)) { continue; }

    uint32_t scripts[4] = {m.roml(e + 2), 0, 0, 0};
    uint8_t n = 1;
    if (scripts[0] == 0) {  // random group of 4
      const uint32_t g = SPAWN_GROUPS + 16 * (uint32_t)rng_13a98(m, 4);
      for (uint8_t k = 0; k < 4; k++) { scripts[k] = m.roml(g + 4 * k); }
      n = 4;
    }
    for (uint8_t k = 0; k < n; k++) {
      const uint32_t a = slot_free_12d6e(m);
      if (!a) { break; }
      m.ww(a + BAND_LO, (uint16_t)(int16_t)lo);
      m.ww(a + BAND_HI, (uint16_t)(int16_t)hi);
      slot_start_12f44(m, a, SPAWN_ADD, scripts[k]);
    }
  }

  for (uint8_t i = 0; i < SLOT_N; i++) {
    const uint32_t a = slotAddr(i);
    if (!m.rb(a + ACTIVE)) { continue; }
    if ((old == m.sw(a + BAND_LO) && nw < m.sw(a + BAND_LO)) ||
        (old == m.sw(a + BAND_HI) && nw > m.sw(a + BAND_HI))) {
      slot_start_12f44(m, a, SPAWN_REMOVE);
    }
  }
}

// 0x1d06a: intermediate wave (tag 6): rewrites up to 4 x 3 dynamic terrain codes
// (Marble Love terrain-wave-update-1d06a.ts, unverified: no trace reaches it)
void wave_1d06a(Mem &m, int8_t n) {
  static const uint32_t tabs[4] = {0x24d5c, 0x24da8, 0x24dce, 0x24df4};
  const int32_t d1 = n <= 0xc ? s8(m.romb(0x24e1a + n)) : n - 6;
  uint32_t src = 0x24c98 + ((((uint32_t)(d1 - 7) & 3) + 7) << 2);
  uint32_t ctl = 0x24c7a + (d1 >> 2);
  uint32_t dst = DYN + (d1 >> 2) * 6;
  for (uint8_t i = 0; i < 4; i++) {
    const int8_t c = s8(m.romb(ctl));
    if (c < 0) { break; }

    const uint32_t k = (uint32_t)(s8(m.romb(src)) * 2);
    uint32_t t[3];
    if (c < 4) {
      t[0] = 0x24d10; t[1] = 0x24cc4; t[2] = tabs[c];
    } else {
      t[0] = 0x24d36; t[1] = 0x24cea; t[2] = 0x24d82;
    }
    for (uint8_t j = 0; j < 3; j++) { m.ww(dst + 2 * j, m.romw(t[j] + k)); }
    src++;
    ctl++;
    dst += 6;
  }
}

// 0x13334: slot animation output. Mode 1 (playfield patch) and 2 (MO + terrain) queue the
// current frame record for applyQueued; modes 0/2 store the screen position and MO frame.
void anim_render_13334(Mem &m, uint32_t a) {
  const uint8_t mode = m.rb(a + MODE);
  if (mode == MODE_PATCH || mode == MODE_MO_TERRAIN) {
    if (gl(m, m.rl(a + ANIM)) == END_MARK) { return; }
    if (mode == MODE_PATCH || m.rb(a + STATE) == ST_ANIM1 || m.rb(a + STATE) == ST_ANIM2) {
      m.wl(ANIM_REQ, m.rl(a + ANIM));
      m.wl(CUR_SLOT, a);
      if (mode == MODE_PATCH) { return; }
    }
  }

  const uint16_t x = m.rw(a + PX), y = m.rw(a + PY);
  m.ww(POS_X, x);
  m.ww(POS_Y, y);
  const uint16_t sx = (uint16_t)(y - x + 0x88);
  const uint16_t sy = (uint16_t)(m.rw(CAMERA) + m.rw(a + PZ) + 0x54 - ((s16(x) + s16(y)) >> 1));
  m.wl(a + SCREEN, (uint32_t)sx << 16 | sy);
  if (m.rb(a + TAG) == TAG_WAVE) { wave_1d06a(m, s8(m.rb(a + LOOP7))); }
  // tag 3: 0x26b66 (palette / banner animation) not ported
  m.wl(a + FRAME, m.rl(a + ANIM));
}

// 0x132e0: next animation frame (+4, modes 1/2 +8); at the end marker wrap to the state's
// start and count +0x1c down (0 = loop forever). True: the script continues.
bool anim_next_132e0(Mem &m, uint32_t a) {
  m.wl(a + ANIM, m.rl(a + ANIM) + (m.rb(a + MODE) ? 8 : 4));
  if (gl(m, m.rl(a + ANIM)) != END_MARK) { return false; }

  m.wl(a + ANIM, m.rl(a + (m.rb(a + STATE) == ST_ANIM2 ? ANIM2 : ANIM1)));
  if (m.rw(a + COUNT) == 0) { return false; }
  m.ww(a + COUNT, m.rw(a + COUNT) - 1);
  return m.rw(a + COUNT) == 0;
}

// word at the script pc, pc += 2
uint16_t arg(Mem &m, uint32_t a) {
  const uint32_t pc = m.rl(a + PC);
  m.wl(a + PC, pc + 2);
  return gw(m, pc);
}

uint32_t shl(int16_t v, uint8_t n) { return (uint32_t)(int32_t)v << n; }

// 0x12896: slot byte code (word opcodes 0-18) until an opcode yields
void script_12896(Mem &m, uint32_t a) {
  for (;;) {
    const int16_t op = s16(arg(m, a));
    if (op < 0 || op > 18) { return; }

    switch (op) {
      case 0: {  // init: position, mode, tag
        const int16_t x = s16(arg(m, a));
        m.wl(a + PX, shl(x, 19));
        const int16_t y = s16(arg(m, a));
        m.wl(a + PY, shl(y, 19));
        const int16_t z = s16(arg(m, a));
        m.wl(a + PZ, shl(z, 16));
        m.wb(a + MODE, (uint8_t)arg(m, a));
        m.wb(a + TAG, (uint8_t)arg(m, a));
        m.wl(a + FRAME, ANIM_NONE);
        if (m.rb(a + MODE) == MODE_PATCH) {  // playfield patch at (col, row)
          m.ww(a + PATCH_COL, arg(m, a));
          m.ww(a + PATCH_ROW, arg(m, a));
          const uint32_t pc = m.rl(a + PC);
          m.wl(a + ANIM2, gl(m, pc));
          m.wl(a + PC, pc + 4);
          m.wl(a + ANIM, m.rl(a + ANIM2));
          anim_render_13334(m, a);
        } else {
          m.wl(a + ANIM, ANIM_NONE);
          marble_drawlist::insert(m, a, m.rb(a + TAG));  // 0x18e6c
          if (m.rb(a + TAG) == TAG_WAVE) {
            m.ww(a + BAND_LO, 0);
            m.ww(a + BAND_HI, 0x500);
            m.wb(WAVE_DONE, 0);
            m.wb(WAVES, m.rb(WAVES) + 1);
            m.wb(a + LOOP7, 0);
          }
        }
        if (m.rb(a + TAG) == TAG_BANNER) {
          m.wl(a + SCREEN, 0xfffffe00);
          m.wl(MASK_TAB0, 0x211fe);
          m.wb(MASK_T0, 2);
          m.wl(MASK_TAB1, 0x2126e);
          m.wb(MASK_T1, 3);
          m.wl(MASK_TAB2, 0x212b2);
          m.wb(MASK_T2, 3);
        }
        break;
      }
      case 1:
      case 2: {  // animate (state 1: +0x4a, 2: +0x46)
        m.ww(a + COUNT, arg(m, a));
        m.wb(a + (op == 1 ? PERIOD1 : PERIOD2), (uint8_t)arg(m, a));
        m.wl(a + ANIM, m.rl(a + (op == 1 ? ANIM1 : ANIM2)));
        m.wb(a + (op == 1 ? DIV1 : DIV2), 0);
        m.wb(a + STATE, (uint8_t)op);
        return;
      }
      case 3:  // wait +0x1c ticks (0 = forever)
        m.ww(a + COUNT, arg(m, a));
        m.wb(a + STATE, ST_WAIT);
        return;
      case 4:
      case 6:  // loop start, count
        m.wb(a + (op == 4 ? LOOP5 : LOOP7), (uint8_t)arg(m, a));
        m.wl(a + (op == 4 ? LOOP5_PC : LOOP7_PC), m.rl(a + PC));
        break;
      case 5:
      case 7: {  // loop end: count 0 = forever
        const uint8_t k = op == 5 ? LOOP5 : LOOP7;
        if (m.rb(a + k)) {
          m.wb(a + k, m.rb(a + k) - 1);
          if (m.rb(a + k) == 0) { break; }
        }
        m.wl(a + PC, m.rl(a + (op == 5 ? LOOP5_PC : LOOP7_PC)));
        break;
      }
      case 8: {  // player within 3 cells: jump
        const uint16_t w = arg(m, a);
        bool hit = false;
        if (w == 0) {
          for (uint16_t k = 0; k < m.rw(PLAYERS); k++) {
            const uint32_t p = PLAYER + PLAYER_SIZE * k;
            if (m.rb(p + ACTIVE) != 1) { continue; }

            const int16_t dx = s16((uint32_t)(s32(m.rl(p + PX) - m.rl(a + PX)) >> 19));
            const int16_t dy = s16((uint32_t)(s32(m.rl(p + PY) - m.rl(a + PY)) >> 19));
            if (0 <= dx && dx < 3 && 0 <= dy && dy < 3) { hit = true; }  // sound 0x31
          }
        }
        if (hit) {
          m.wl(a + PC, gl(m, m.rl(a + PC)));
        } else {
          m.wl(a + PC, m.rl(a + PC) + 2);
        }
        break;
      }
      case 9:
        m.wl(a + PC, gl(m, m.rl(a + PC)));
        break;
      case 10: {
        const uint32_t pc = m.rl(a + PC);
        m.wl(a + PC, gl(m, pc));
        m.wl(a + RET_PC, pc + 4);
        break;
      }
      case 11:
        m.wl(a + PC, m.rl(a + RET_PC));
        break;
      case 12:
      case 13: {  // animation for state 2 / 1
        const uint32_t pc = m.rl(a + PC);
        const uint32_t v = gl(m, pc);
        m.wl(a + (op == 12 ? ANIM2 : ANIM1), v);
        m.wl(a + ANIM, v);
        m.wl(a + PC, pc + 4);
        break;
      }
      case 14: {
        const int16_t vx = s16(arg(m, a));
        m.wl(a + VX, shl(vx, 8));
        const int16_t vy = s16(arg(m, a));
        m.wl(a + VY, shl(vy, 8));
        break;
      }
      case 15:
        slot_start_12f44(m, a, SPAWN_REMOVE);
        return;
      case 16:
        m.wl(a + PX, m.rl(a + PX) + m.rl(a + VX));
        m.wl(a + PY, m.rl(a + PY) + m.rl(a + VY));
        break;
      case 17:  // wait for a player in zone
        m.wl(a + RET_PC, m.rl(a + PC) - 2);
        m.wb(a + STATE, ST_ZONE);
        m.wb(a + ZONE, (uint8_t)arg(m, a));
        return;
      case 18: {  // no player in zone (or zone + 1 >= 0x1e): jump
        bool free = true;
        for (uint16_t k = 0; k < m.rw(PLAYERS); k++) {
          const uint32_t p = PLAYER + PLAYER_SIZE * k;
          if (m.rb(p + ACTIVE) != 1) { continue; }

          const int8_t pz = s8(m.rb(p + ZONE)), az = s8(m.rb(a + ZONE));
          if (m.rb(a + ZONE) == m.rb(p + ZONE) || (pz == az + 1 && pz >= 0x1e)) {
            free = false;
          }
        }
        const uint32_t pc = m.rl(a + PC);
        if (free) {  // sound 0x1ef5a
          m.wl(a + PC, gl(m, pc));
        } else {
          m.wl(a + PC, pc + 4);
        }
        break;
      }
    }
  }
}

// tag 3 (beginner) steps three global animation pointers (0x40044a/e/52)
enum Wrap : uint8_t { WRAP_0, WRAP_1, WRAP_2 };

void tag3_step(Mem &m, uint32_t ptr, uint32_t timer, uint8_t reload, Wrap wrap) {
  m.wb(timer, m.rb(timer) - 1);
  if (m.rb(timer)) { return; }

  m.wb(timer, reload);
  m.wl(ptr, m.rl(ptr) + 4);
  if (gl(m, m.rl(ptr)) != END_MARK) { return; }

  switch (wrap) {
    case WRAP_0:
      if (gl(m, m.rl(MASK_TAB0) - 4) == 0x211f2) {
        m.wl(MASK_TAB0, 0x2121e);
        m.wb(MASK_T0, m.rb(MASK_T0) + 0xf);
      } else {
        m.wl(MASK_TAB0, 0x211fe);
        m.wb(MASK_T0, m.rb(MASK_T0) + 0x1e);
      }
      break;
    case WRAP_1: m.wl(MASK_TAB1, 0x2126e); break;
    case WRAP_2: m.wl(MASK_TAB2, 0x212b2); break;
  }
}

void tag3_globals(Mem &m) {
  tag3_step(m, MASK_TAB0, MASK_T0, 2, WRAP_0);
  tag3_step(m, MASK_TAB1, MASK_T1, 3, WRAP_1);
  tag3_step(m, MASK_TAB2, MASK_T2, 3, WRAP_2);
}

// 0x13068: one logic tick of a slot (state 0 wait, 1/2 animate, 3 start, 4 zone wait)
void slot_step_13068(Mem &m, uint32_t a) {
  if (!m.rb(a + ACTIVE)) { return; }

  uint8_t d3 = m.rb(a + STATE);
  bool d2 = false;
  if (m.rb(a + TAG) == TAG_BANNER) { tag3_globals(m); }

  for (;;) {
    switch (m.rb(a + STATE)) {
      case ST_START:
        m.wl(a + PC, m.rl(a + SCRIPT));
        m.wl(a + ANIM1, ANIM_NONE);
        m.wl(a + ANIM2, ANIM_NONE);
        m.wl(a + ANIM, ANIM_NONE);
        script_12896(m, a);
        break;
      case ST_WAIT:
        if (m.rw(a + COUNT)) {
          m.ww(a + COUNT, m.rw(a + COUNT) - 1);
          if (m.rw(a + COUNT) == 0 && m.rb(a + ACTIVE) == 1) { script_12896(m, a); }
        }
        d2 = true;
        break;
      case ST_ZONE:
        m.wl(a + ANIM, ANIM_NONE);
        d2 = true;
        break;
      case ST_ANIM1:
        if (m.rb(a + PERIOD1)) { m.wb(a + DIV1, m.rb(a + DIV1) + 1); }
        if (m.rb(a + PERIOD1) != m.rb(a + DIV1)) {
          d2 = true;
          break;
        }
        m.wb(a + DIV1, 0);
        m.wl(a + PX, m.rl(a + PX) + m.rl(a + VX));
        m.wl(a + PY, m.rl(a + PY) + m.rl(a + VY));
        anim_render_13334(m, a);
        if (anim_next_132e0(m, a)) { script_12896(m, a); }
        break;
      case ST_ANIM2:
        if (m.rb(a + PERIOD2)) { m.wb(a + DIV2, m.rb(a + DIV2) + 1); }
        if (m.rb(a + PERIOD2) != m.rb(a + DIV2)) {
          d2 = true;
          break;
        }
        m.wb(a + DIV2, 0);
        if (m.rb(a + TAG) == TAG_WAVE) {
          m.wb(a + LOOP7, m.rb(a + LOOP7) + 1);
          if (m.rb(a + LOOP7) == 0x1e) { m.wb(WAVE_DONE, 1); }
        }
        if (m.rb(a + TAG) == TAG_ULTIMATE) {
          const int64_t d = ((int64_t)m.rl(a + ANIM) - m.rl(a + ANIM2)) >> 3;
          m.ww(ULT_COL, (uint16_t)(d * 0x90));
        }
        anim_render_13334(m, a);
        if (anim_next_132e0(m, a)) { script_12896(m, a); }
        break;
      default:
        break;
    }

    const uint8_t st = m.rb(a + STATE);
    if (st != d3 && st == ST_ANIM1 && !m.rb(a + PERIOD1)) {
      d3 = st;
      continue;
    }
    break;
  }
  if (m.rb(a + MODE) != MODE_PATCH && d2) { anim_render_13334(m, a); }
}

// walls 0x1a-0x1f: ranges along / perpendicular to the wall, ends where it reflects the
// motion along it
struct Wall {
  uint8_t tag;
  bool alongX;
  int8_t al0, al1, pe0, pe1;
  int8_t e0a, e0b, e1a, e1b;
};
static const Wall WALLS[] = {
    {0x1b, true,  -8,    0x20, -0xc, 0,  -8,   0,  0x18, 0x20},
    {0x1c, true,  -8,    0x20, -8,   4,  -8,   0,  0x18, 0x20},
    {0x1e, true,  -0xc,  0x5e, -0xe, 0,  -0xc, -4, 0x56, 0x5e},
    {0x1a, false, -8,    0x20, -0xc, 0,  -8,   0,  0x18, 0x20},
    {0x1d, false, -8,    0x20, -8,   4,  -8,   0,  0x18, 0x20},
    {0x1f, false, -0xc,  0x54, -0xe, 0,  -0xc, -4, 0x4c, 0x54},
};

// cell rectangles that set the surface code +0x58: 0 <= slot cell - cell < w, h
struct Rect {
  uint8_t tag, w, h;
  bool sameZ;
};
static const Rect RECTS[] = {
    {0x10, 0x10, 0xe,  false}, {0x32, 4,    2,    false}, {0x33, 2,    4,   false},
    {0x34, 8,    0xb,  false}, {0x35, 3,    8,    false}, {0x36, 6,    3,   false},
    {0x37, 2,    3,    false}, {0x2d, 0x14, 0xe,  true},  {0x2e, 2,    3,   true},
    {0x3b, 2,    3,    true},  {0x38, 0x14, 0xe,  true},  {0x39, 0xc,  0xe, true},
    {0x3a, 4,    2,    true},  {0x2f, 0xa,  0x14, true},  {0x30, 0x13, 9,   true},
    {0x31, 2,    6,    true},
};

// surface tags in bit masks (tags < 64)
#define TAG_BIT(t) (1ull << (t))
static const uint64_t QUIET = TAG_BIT(0x10) | TAG_BIT(0x17) | TAG_BIT(0x18) | TAG_BIT(0x32) |
    TAG_BIT(0x33) | TAG_BIT(0x34) | TAG_BIT(0x35) | TAG_BIT(0x36) | TAG_BIT(0x37);
static const uint64_t KEEP58 = QUIET | TAG_BIT(0) | TAG_BIT(0x3b) | TAG_BIT(0x2d) |
    TAG_BIT(0x2e) | TAG_BIT(0x38) | TAG_BIT(0x39) | TAG_BIT(0x3a) | TAG_BIT(0x2f) |
    TAG_BIT(0x30) | TAG_BIT(0x31);

#undef TAG_BIT

inline bool inMask(uint64_t mask, uint8_t tag) { return tag < 64 && ((mask >> tag) & 1); }

void setSurface(Mem &m, uint32_t a2, uint8_t tag) {
  m.wb(a2 + P_SURFACE, tag);
  m.wb(a2 + P_SURFACE_T, 0xff);
}

inline int32_t absl(int32_t v) { return v < 0 ? -v : v; }

// contact of the player (a2) with one slot s, in the order of the model
void contact(Mem &m, uint32_t a2, uint32_t s, int32_t vx, int32_t vy) {
  const int16_t mcx = m.sw(CELL_X), mcy = m.sw(CELL_Y), mx = m.sw(POS_X), my = m.sw(POS_Y);
  const int16_t sx = m.sw(s + PX), sy = m.sw(s + PY);
  const int16_t d6 = s16((uint32_t)(sx - mx)), a0 = s16((uint32_t)(sy - my));
  const int16_t d1 = s16((uint32_t)((sx >> 3) - mcx)), d2 = s16((uint32_t)((sy >> 3) - mcy));
  const int8_t tag = s8(m.rb(s + TAG));
  if (tag < TAG_BOUNDS_LO || tag > TAG_BOUNDS_HI) { return; }

  for (const Wall &w : WALLS) {
    if (w.tag != tag) { continue; }

    const int16_t al = w.alongX ? d6 : a0, pe = w.alongX ? a0 : d6;
    const int32_t v = w.alongX ? vx : vy;
    if (!(w.al0 < al && al < w.al1 && w.pe0 < pe && pe < w.pe1)) { return; }

    if ((w.e0a < al && al < w.e0b && v < 0) || (w.e1a < al && al < w.e1b && v > 0)) {
      m.wb(w.alongX ? HIT_X : HIT_Y, 1);
    }
    m.wb(w.alongX ? HIT_Y : HIT_X, 1);  // sound 0x42
    return;
  }

  for (const Rect &r : RECTS) {
    if (r.tag != tag) { continue; }

    if (0 <= d1 && d1 < r.w && 0 <= d2 && d2 < r.h &&
        (!r.sameZ || m.rl(s + PZ) == m.rl(a2 + P_PZ))) {
      setSurface(m, a2, tag);
    }
    return;
  }

  if (tag == TAG_POST) {  // round post: |d| < 3.5 px reflects both axes
    const uint16_t ax = (uint16_t)(absl(d6) << 4), ay = (uint16_t)(absl(a0) << 4);
    const uint16_t dist = ax > ay ? (uint16_t)((ay >> 3) * 3 + ax) : (uint16_t)((ax >> 3) * 3 + ay);
    if (dist < 0x38) {
      m.wb(HIT_Y, 1);
      m.wb(HIT_X, 1);  // sound 0x42
    }
  } else if (tag == TAG_ROW_LO) {
    if (d1 == 0 && -0x12 < d2 && d2 <= 0) { setSurface(m, a2, tag); }
  } else if (tag == TAG_ROW_HI) {
    if (-0x12 < d1 && d1 <= 0 && d2 == 0) { setSurface(m, a2, tag); }
  }
}

}  // namespace

// 0x29cce: collision with the level object slots. Ported: wall segments (0x1a-0x1f), round
// posts (0x05, Marble Love sub-29cce.ts) and cell rectangles that set the surface code +0x58.
// Not ported: 0x0a catapult, 0x0b/0x0d aerial gates, 0x0c, tubes 0x12-0x16 and 0x20-0x27
// (some return from 0x29cce early, skipping the bounce epilogue).
void slots_29cce(Mem &m, uint32_t a2) {
  const int32_t vx = m.sl(a2 + P_VX), vy = m.sl(a2 + P_VY);
  m.wb(a2 + P_SURFACE, 0);

  for (uint8_t i = 0; i < SLOT_N; i++) {
    const uint32_t s = slotAddr(i);
    if (!m.rb(s + ACTIVE)) { continue; }

    contact(m, a2, s, vx, vy);
    if (!inMask(KEEP58, m.rb(a2 + P_SURFACE))) { break; }
  }

  // sounds 0x43 / 0x44 on entering / leaving a quiet surface not ported
  if (m.rb(HIT_X)) {
    m.wl(a2 + PX, m.rl(OLD_X));
    m.wl(a2 + P_VX, 0u - m.rl(a2 + P_VX));
  }
  if (m.rb(HIT_Y)) {
    m.wl(a2 + PY, m.rl(OLD_Y));
    m.wl(a2 + P_VY, 0u - m.rl(a2 + P_VY));
  }
}

// 0x144e4: camera move: band = (camera - descriptor top) >> 4. Enemy spawners 0x15a12,
// 0x14c46, 0x17346 and the aerial extras are not ported.
void cameraBands(Mem &m, int16_t oldCam, int16_t newCam) {
  const int16_t top = (int16_t)m.romw(m.desc() + DESC_TOP);
  const int16_t a = (int16_t)(oldCam - top) >> 4, b = (int16_t)(newCam - top) >> 4;
  if ((a & 0xff) != (b & 0xff)) { spawn_12dfa(m, a, b); }
}

// 0x12fd0: per tick slot update: intermediate wave trigger, dynamic code reload (0x11ac2)
// while a wave runs, then every slot in index order
void update(Mem &m) {
  if (m.rw(RACE) == RACE_INTERMEDIATE) {
    for (uint16_t k = 0; k < m.rw(PLAYERS); k++) {
      const uint32_t p = PLAYER + PLAYER_SIZE * k;
      if (m.rb(p + ACTIVE) != 1 || !m.rb(WAVE_DONE)) { continue; }
      if (m.rb(p + ZONE) != 9 && m.rb(p + ZONE) != 0xa) { continue; }

      const uint32_t a = slot_free_12d6e(m);
      if (a) { slot_start_12f44(m, a, SPAWN_ADD, WAVE_SCRIPT); }
      break;
    }
  }
  if (m.rb(WAVES)) {
    for (uint8_t k = 0; k < DYN_INTERMEDIATE_N; k++) {
      m.ww(DYN + 2 * k, m.romw(DYN_INTERMEDIATE + 2 * k));
    }
  }
  for (uint8_t i = 0; i < SLOT_N; i++) { slot_step_13068(m, slotAddr(i)); }
}

// 0x1344c: the queued frame record (long playfield tile block, long dynamic terrain block)
// is applied. Terrain block: word first code slot, word count, words -> 0x40076e. The tile
// block (mode 1 only) is for the renderer: lastPatch() returns its record. The arcade stores
// it (0x400978) only with rows on screen; the renderer clips instead.
void applyQueued(Mem &m) {
  const uint32_t rec = m.rl(ANIM_REQ);
  if (!rec) { return; }

  const uint32_t obj = m.rl(CUR_SLOT);
  if (obj && m.rb(obj + MODE) == MODE_PATCH) { m.wl(PATCHED, rec); }

  const uint32_t dyn = gl(m, rec + 4);
  if (dyn) {
    const int16_t first = s16(gw(m, dyn));
    const uint16_t n = gw(m, dyn + 2);
    for (uint16_t k = 0; k < n; k++) { m.ww(DYN + 2 * (first + k), gw(m, dyn + 4 + 2 * k)); }
  }
  m.wl(ANIM_REQ, 0);
}

uint32_t lastPatch(const Mem &m) { return m.rl(PATCHED); }

// level init 0x105a0-0x10614 (slot part): camera at the race top (silly: bottom), spawn the
// bands the camera enters from 0x19 px outside (0x144e4), one slot update (0x12fd0), then
// 0x1344c
void init(Mem &m, uint8_t race) {
  // +0x19 = slot number: the arcade sets it once at boot (slot array init, 0x400a9c), the
  // work RAM is cleared at race start here
  for (uint8_t i = 0; i < SLOT_N; i++) { m.wb(slotAddr(i) + IDX, i); }
  m.ww(RACE, race);
  m.ww(PLAYERS, 1);
  m.ww(DYN_PAUSE, 0xffff);
  m.wb(DYN_FLAG, 0);

  const uint32_t cam = m.romw(m.desc() + DESC_TOP) +
      (race == RACE_SILLY ? m.romw(m.desc() + DESC_RANGE) : 0);
  m.wl(CAMERA_L, cam);
  if (race == RACE_INTERMEDIATE) {
    for (uint8_t k = 0; k < DYN_INTERMEDIATE_N; k++) {
      m.ww(DYN + 2 * k, m.romw(DYN_INTERMEDIATE + 2 * k));
    }
  }

  const uint32_t old = race == RACE_SILLY ? cam + 0x19 : cam - 0x19;
  cameraBands(m, (int16_t)old, (int16_t)cam);
  update(m);
  applyQueued(m);
}

}  // namespace marble_objects

#endif  // ENABLE_MARBLE
