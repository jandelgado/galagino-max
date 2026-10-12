#include "marble_objects.h"
#include "marble_enemies.h"

#ifdef ENABLE_MARBLE

#include "marble_drawlist.h"
#include "marble_physics.h"
#include "marble_rng.h"

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
      const uint32_t g = SPAWN_GROUPS + 16 * (uint32_t)marble_rng::rng(m, 4);
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
// Tubes (Marble Love sub-29cce.ts runTube12..runTube26Or27, helper-1cd00.ts). An entry
// teleports the marble to a fixed exit (the second exit if 0x17cb8 finds the first taken),
// state TUBE: holes 0x12 (beginner), 0x20 (intermediate), 0x25 (ultimate), entries 0x13/0x14
// (beginner), the silly funnel 0x22. The other tube tags are walls (shape 0x1cd00).
static const uint8_t TAG_HOLE_B = 0x12, TAG_ENTRY_A = 0x13, TAG_ENTRY_B = 0x14;
static const uint8_t TAG_HOLE_I = 0x20, TAG_FUNNEL = 0x22, TAG_HOLE_U = 0x25;
static const uint32_t SHAPES = 0x24c5e;       // 0x1cd00 shape pointers: 16 byte plane entries
static const uint8_t SHAPE_RIM = 0, SHAPE_ENTRY = 2;  // hole rims, 0x13/0x14 mouth walls
static const uint32_t POS_Z = 0x400694, OLD_Z = 0x40068c;  // word: z of POS_X/Y; long
static const uint8_t P_VZ = 0x08, P_PX = 0x0c, P_PY = 0x10, P_AIR = 0x36, P_DAMAGE = 0x57;
static const uint8_t P_STATE = 0x1a, P_GROUND_Z = 0x2a;
static const uint8_t AIRBORNE = 2, FELL = 0x64;  // +0x36; +0x57 at the LOST entry
static const uint16_t FUNNEL_FLOOR = 0x3f74, FUNNEL_DROP = 0x3f94;  // z words
static const uint16_t EXIT_X0 = 0x2dc, EXIT_X1 = 0x2ac, EXIT_Y = 0x2b0, EXIT_FREE = 0x70;
static const uint32_t EXIT_V = 0x40000, EXIT_VY = 0xfffc0000;  // 4 px per tick, -4 px
static const uint8_t EXIT_TICKS = 0x12;       // TUBE state length
static const uint32_t LIFT_Z = 0x20000, LIFT_VZ = 0x9000;
static const int32_t SHAPE_NEAR = 0x400, FALL_LOSS = 0x100000;  // plane distance; 16 px

// Tube walls: shape k when the slot is within d6/a0 px (cells: d1/d2) and the marble below z
// (0: any z)
struct TubeWall {
  uint8_t tag;
  bool cells;
  int8_t x0, x1, y0, y1;  // x0 <= d < x1
  uint16_t below;
  uint8_t shape;
};
static const TubeWall TUBE_WALLS[] = {
    {0x15, false, -0x18, 0x28, -0x18, 0x28, 0x3f40, 1},
    {0x16, false, -0x10, 0x20, -0x10, 0x18, 0, 3},
    {0x21, true, -1, 4, -1, 3, 0x3f60, 4},
    {0x23, false, -0x10, 0x20, -0x18, 0x28, 0, 5},
    {0x24, false, -0x10, 0x20, -0x18, 0x28, 0, 5},
    {0x26, false, -0x18, 0x28, -0x18, 0x28, 0, 6},
    {0x27, false, -0x18, 0x28, -0x18, 0x28, 0, 6},
};

// Holes 0x12 / 0x20 by z word: above `entry` the hole acts, at or above `walls` its rim
// (shape 0), at or above `stop` the marble stops (drops), below it leaves by the exit
struct TubeHole {
  uint8_t tag;
  uint16_t entry, walls, stop;
};
static const TubeHole TUBE_HOLES[] = {
    {TAG_HOLE_B, 0x3f40, 0x3f78, 0x3f60},
    {TAG_HOLE_I, 0x3f80, 0x3fb0, 0x3fa0},
};

enum ExitZ { KEEP_OLD_Z, SET_OLD_Z };  // OLD_Z: the 0x22 funnel only
enum ExitTag { KEEP_TAG, SET_TAG };    // 0x12 keeps the surface its hole cells set

// teleport: marble at (x, y) on its cell floor, surface = the slot tag, TUBE (sounds 0x15884
// and 0x35 / 0x36)
void tubeExit(Mem &m, uint32_t a2, uint32_t s, uint16_t x, uint16_t y, ExitZ oz = SET_OLD_Z,
              ExitTag tag = SET_TAG) {
  m.wl(a2 + P_PX, (uint32_t)x << 16);
  m.wl(a2 + P_PY, (uint32_t)y << 16);
  const uint32_t z = (uint32_t)marble_physics::floorZ(m, a2);
  m.wl(a2 + P_PZ, z);
  if (oz == SET_OLD_Z) { m.wl(OLD_Z, z); }
  if (tag == SET_TAG) { m.wb(a2 + P_SURFACE, m.rb(s + TAG)); }
  m.wb(a2 + P_STATE, marble_physics::TUBE);
}

// exit y (or x) a, b if a is taken by another object near (x, y)
uint16_t freeExit(Mem &m, uint32_t a2, int16_t x, int16_t y, uint16_t a, uint16_t b) {
  return marble_physics::blocked(m, a2, x, y, EXIT_FREE) ? b : a;
}

void launch(Mem &m, uint32_t a2, uint32_t vx, uint32_t vy) {
  m.wb(a2 + P_AIR, 0);
  m.wb(a2 + P_SURFACE_T, EXIT_TICKS);
  m.wl(a2 + P_VX, vx);
  m.wl(a2 + P_VY, vy);
}

// 0x22 funnel: on the cell before the slot cell a marble above FUNNEL_DROP drops out of an
// exit (first by rng(2), the other if that one is blocked); on the funnel floor it is lifted
void funnel_22(Mem &m, uint32_t a2, uint32_t s, int16_t d1, int16_t d2, uint16_t z) {
  if (d1 != 1 || d2 != 1) { return; }

  if (z > FUNNEL_DROP) {
    m.wb(a2 + P_AIR, 0);
    m.wb(a2 + P_SURFACE_T, EXIT_TICKS);
    m.wl(a2 + P_VX, 0);
    m.wl(a2 + P_VY, EXIT_VY);
    const bool first = marble_rng::rng(m, 2) != 0;
    const uint16_t a = first ? EXIT_X0 : EXIT_X1, b = first ? EXIT_X1 : EXIT_X0;
    const bool taken = marble_physics::blocked(m, a2, (int16_t)a, (int16_t)EXIT_Y, EXIT_FREE);
    tubeExit(m, a2, s, taken ? b : a, EXIT_Y);
    return;
  }
  if (z == FUNNEL_FLOOR) { m.wl(a2 + P_PZ, m.rl(a2 + P_PZ) + LIFT_Z); }
  m.wl(a2 + P_VZ, m.rl(a2 + P_VZ) + LIFT_VZ);
  m.wl(a2 + P_VY, 0);
  m.wl(a2 + P_VX, 0);
  m.wb(a2 + P_AIR, AIRBORNE);
}

// back to the position before the move (OLD_X/Y/Z)
void restoreOld(Mem &m, uint32_t a2) {
  m.wl(a2 + P_PX, m.rl(OLD_X));
  m.wl(a2 + P_PY, m.rl(OLD_Y));
  m.wl(a2 + P_PZ, m.rl(OLD_Z));
}

// 0x1cd00 response (L_01CF36) to plane (nx, ny, nz): -1 next entry, else the result. Deep
// (old position 0x400 or more off the plane): old position, the velocity loses its normal
// part. Shallow: a fall over 16 px loses the marble (1), else old position, velocity reversed.
int8_t shapeHit(Mem &m, uint32_t a2, int16_t nx, int16_t ny, int16_t nz, int32_t ad6,
                const uint32_t v0[3]) {
  if (ad6 >= SHAPE_NEAR) {
    restoreOld(m, a2);
    const int32_t vx = m.sl(a2 + P_VX), vy = m.sl(a2 + P_VY), vz = m.sl(a2 + P_VZ);
    const int32_t sum = ny * s16((uint32_t)(vy >> 7)) + nx * s16((uint32_t)(vx >> 7)) +
                        nz * s16((uint32_t)(vz >> 7));
    const int16_t dot = s16((uint32_t)(sum >> 8));
    m.wl(a2 + P_VX, (uint32_t)vx - (uint32_t)(nx * dot));
    m.wl(a2 + P_VY, (uint32_t)vy - (uint32_t)(ny * dot));
    m.wl(a2 + P_VZ, (uint32_t)vz - (uint32_t)(nz * dot));
    return -1;
  }

  const int32_t fall = s32(m.rl(a2 + P_PZ) - m.rl(a2 + P_GROUND_Z));
  if (m.rb(a2 + P_AIR) == AIRBORNE && (fall < 0 ? -(int64_t)fall : fall) > FALL_LOSS) {
    if (marble_physics::isPlayer(a2)) {  // sound 0x46
      m.wb(a2 + P_DAMAGE, FELL);
      marble_physics::enterState(m, a2, marble_physics::LOST);
    } else {
      marble_enemies::remove_15bd0(m, a2, marble_enemies::SCORE, marble_enemies::CLEAR);
    }
    return 1;
  }
  m.wl(a2 + P_VX, 0u - v0[0]);
  m.wl(a2 + P_VY, 0u - v0[1]);
  m.wl(a2 + P_VZ, 0u - v0[2]);
  restoreOld(m, a2);
  return 0;
}

// a, b, c within the entry's x, y, z box (bytes 6-11: min, max pairs)
bool inBox(const Mem &m, uint32_t e, int32_t a, int32_t b, int32_t c) {
  return s8(m.romb(e + 6)) <= a && a <= s8(m.romb(e + 7)) && s8(m.romb(e + 8)) <= b &&
         b <= s8(m.romb(e + 9)) && s8(m.romb(e + 10)) <= c && c <= s8(m.romb(e + 11));
}

// 0x1cd00: the marble's new (POS_X/Y/Z) and old (OLD_X/Y/Z) position against shape k at
// slot s (cell corner + 8): plane entries {nx, ny, nz words; box; point cx, cy, cz bytes},
// nx > 0x800 marks the last (nx - 0x1000). A position in a box with the plane crossed or
// near: shapeHit. Returns 1 when the marble is lost.
uint8_t shape_1cd00(Mem &m, uint32_t a2, uint32_t s, uint8_t k) {
  static const int16_t LAST = 0x800, LAST_BIAS = 0x1000;
  const uint32_t v0[3] = {m.rl(a2 + P_VX), m.rl(a2 + P_VY), m.rl(a2 + P_VZ)};
  const uint16_t rx = (uint16_t)(m.sw(s + PX) + 8), ry = (uint16_t)(m.sw(s + PY) + 8);
  const int16_t rz = m.sw(s + PZ);
  const int16_t x1 = s16((uint32_t)(rx - m.rw(POS_X))), y1 = s16((uint32_t)(ry - m.rw(POS_Y)));
  const int32_t z1 = rz - m.sw(POS_Z);
  const int16_t x2 = s16((uint32_t)(rx - m.rw(OLD_X))), y2 = s16((uint32_t)(ry - m.rw(OLD_Y)));
  const int16_t z2 = s16((uint32_t)(rz - m.rw(OLD_Z)));

  for (uint32_t e = m.roml(SHAPES + 4 * k);; e += 16) {
    int16_t nx = s16(m.romw(e));
    const bool last = nx > LAST;
    if (last) { nx = s16((uint32_t)(nx - LAST_BIAS)); }
    const int16_t ny = s16(m.romw(e + 2)), nz = s16(m.romw(e + 4));
    const int8_t cx = s8(m.romb(e + 12)), cy = s8(m.romb(e + 13)), cz = s8(m.romb(e + 14));

    if (inBox(m, e, x1, y1, z1) || inBox(m, e, x2, y2, z2)) {
      const int32_t d5 = nz * s16((uint32_t)(z1 - cz)) + ny * s16((uint32_t)(y1 - cy)) +
                         nx * s16((uint32_t)(x1 - cx));
      const int32_t d6 = nz * s16((uint32_t)(z2 - cz)) + ny * s16((uint32_t)(y2 - cy)) +
                         nx * s16((uint32_t)(x2 - cx));
      const int32_t a5 = d5 < 0 ? -d5 : d5, a6 = d6 < 0 ? -d6 : d6;
      if (a5 < SHAPE_NEAR || a6 < SHAPE_NEAR || (d5 ^ d6) < 0) {
        const int8_t r = shapeHit(m, a2, nx, ny, nz, a6, v0);
        if (r >= 0) { return (uint8_t)r; }
      }
    }
    if (last) { return 0; }
  }
}

// 0x12 / 0x20 hole over cells d1 -2..2, d2 -1..4 of the slot; 1: the marble is lost
uint8_t tubeHole(Mem &m, uint32_t a2, uint32_t s, const TubeHole &h, int16_t d1, int16_t d2,
                 uint16_t z) {
  if (d1 < -2 || d1 >= 3 || d2 < -1 || d2 >= 5 || z <= h.entry) { return 0; }

  if (0 <= d1 && 0 <= d2 && d2 < 4) { setSurface(m, a2, h.tag); }
  if (z >= h.walls) { return shape_1cd00(m, a2, s, SHAPE_RIM); }
  if (d1 < 0) { return 0; }
  if (z >= h.stop) {
    m.wl(a2 + P_VY, 0);
    m.wl(a2 + P_VX, 0);
    return 0;
  }

  if (h.tag == TAG_HOLE_I) {  // out of the intermediate exit, upwards
    launch(m, a2, 0, EXIT_VY);
    tubeExit(m, a2, s, 0x26c, freeExit(m, a2, 0x26c, 0x254, 0x254, 0x25c), KEEP_OLD_Z);
    return 0;
  }
  if (!marble_physics::isPlayer(a2)) {  // a steelie falls through
    marble_enemies::remove_15bd0(m, a2, marble_enemies::SCORE, marble_enemies::CLEAR);
    return 0;
  }
  launch(m, a2, EXIT_V, 0);
  tubeExit(m, a2, s, freeExit(m, a2, 0x20c, 0x264, 0x20c, 0x204), 0x264, KEEP_OLD_Z, KEEP_TAG);
  return 0;
}

// 0x13 / 0x14 beginner: rolling onto the slot row leaves by the exit below; else mouth walls
uint8_t tubeEntry(Mem &m, uint32_t a2, uint32_t s, int16_t d1, int16_t d2, int16_t d6,
                  int16_t a0) {
  if (d6 < -0x18 || d6 >= 0x20 || a0 < -0x10 || a0 >= 0x18) { return 0; }
  if (d2 != 0 || d1 < 0 || d1 >= 3) { return shape_1cd00(m, a2, s, SHAPE_ENTRY); }

  m.wb(a2 + P_SURFACE_T, EXIT_TICKS);
  const uint16_t y = freeExit(m, a2, 0x29c, 0x2e4, 0x2e4, 0x2dc);
  m.wl(a2 + P_VX, 0);
  m.wl(a2 + P_VY, EXIT_V);
  tubeExit(m, a2, s, 0x29c, y, KEEP_OLD_Z);
  return 0;
}

// 0x25 ultimate hole (cells 0..1 of the slot, below 0x3fe0): out of one of two exits
void tubeHoleU(Mem &m, uint32_t a2, uint32_t s, int16_t d1, int16_t d2, uint16_t z) {
  static const uint16_t BOTTOM = 0x3fe0, X = 0xf0, Y0 = 0x100, Y1 = 0x140;
  if (d1 < 0 || d1 > 1 || d2 < 0 || d2 > 1 || z >= BOTTOM) { return; }

  m.wb(a2 + P_AIR, 0);  // sound 0x46
  m.wb(a2 + P_SURFACE_T, EXIT_TICKS);
  const bool first = marble_rng::rng(m, 2) != 0;
  const uint16_t a = first ? Y0 : Y1, b = first ? Y1 : Y0;
  const uint16_t y = freeExit(m, a2, X, a, a, b);
  m.wl(a2 + P_VX, EXIT_V);
  m.wl(a2 + P_VY, 0);
  tubeExit(m, a2, s, X, y, KEEP_OLD_Z);
}

// tube wall tags: true (and the result in lost) when tag is one
bool tubeWall(Mem &m, uint32_t a2, uint32_t s, uint8_t tag, int16_t d1, int16_t d2, int16_t d6,
              int16_t a0, uint8_t &lost) {
  for (const TubeWall &w : TUBE_WALLS) {
    if (w.tag != tag) { continue; }

    const int16_t dx = w.cells ? d1 : d6, dy = w.cells ? d2 : a0;
    lost = 0;
    if (dx < w.x0 || dx >= w.x1 || dy < w.y0 || dy >= w.y1) { return true; }
    if (w.below && m.rw(POS_Z) >= w.below) { return true; }
    lost = shape_1cd00(m, a2, s, w.shape);
    return true;
  }
  return false;
}

// Aerial objects (Marble Love sub-29cce.ts runCatapult0A, runGate0B/0D, runBounce0C; MARBLE.md
// "Aerial objects"): vacuum gates 0x0b/0x0d pull and catch the marble (CAUGHT), the catapult
// 0x0a throws it, bounce boxes (TAG_CROWD) make it dizzy
static const uint8_t TAG_CATAPULT = 0x0a, TAG_GATE_B = 0x0b, TAG_GATE_D = 0x0d;
static const uint32_t GATE_OPEN_B = 0x22016, GATE_OPEN_D = 0x220a6;    // ANIM2 while sucking
static const uint32_t GATE_CLOSE_B = 0x1d752, GATE_CLOSE_D = 0x1d798;  // 0x13892, 0x2563e
static const uint32_t CATAPULT_SCRIPT = 0x1db80;
static const uint16_t GATE_TOP = 0x3fc4;      // z word: the gate frame is above
static const uint8_t CAUGHT_TICKS = 0x20;     // +0x57: length of CAUGHT
static const uint8_t THROW_TICKS = 0x0f;      // +0x59: catapult TUBE state length
static const uint32_t THROW_DZ = 0x30000, THROW_VZ = 0xa0000;
static const uint16_t THROW_VX = 0x2000, THROW_VY = 0xa000;  // rng ranges
static const int32_t THROW_VY0 = -0x25000;
static const uint8_t BOX_DAMAGE = 0x3c, BOX_MARGIN = 3;
static const uint8_t P_DIZZY_CNT = 0x56, P_ANIM = 0x5a, P_ANIM_5F = 0x5f, P_ANIM_60 = 0x60;
static const uint32_t ANIM_DIZZY = 0x20faa;
static const uint8_t ANIM_RATE = 2, EMBEDDED = 0x65;

// x0 <= x < x1 and y0 <= y < y1
inline bool within(int16_t x, int16_t y, int16_t x0, int16_t x1, int16_t y0, int16_t y1) {
  return x0 <= x && x < x1 && y0 <= y && y < y1;
}

// 0x29cce distance: max + 3/8 min of |dx| * 16, |dy| * 16 (word)
uint16_t distance(int16_t dx, int16_t dy) {
  const uint16_t ax = (uint16_t)(absl(dx) << 4), ay = (uint16_t)(absl(dy) << 4);
  return ax > ay ? (uint16_t)((ay >> 3) * 3 + ax) : (uint16_t)((ax >> 3) * 3 + ay);
}

// (dx, dy) / distance * 0x1000 (divs.w), 0 when the distance is 0
void unit(int16_t dx, int16_t dy, int16_t &ux, int16_t &uy) {
  const int32_t d = distance(dx, dy);
  ux = d ? (int16_t)(((int32_t)dx << 16) / d) : 0;
  uy = d ? (int16_t)(((int32_t)dy << 16) / d) : 0;
}

// the slot relative to the marble's position before the move
void oldDelta(const Mem &m, uint32_t s, int16_t &d6, int16_t &a0) {
  d6 = s16((uint32_t)(m.rw(s + PX) - m.rw(OLD_X)));
  a0 = s16((uint32_t)(m.rw(s + PY) - m.rw(OLD_Y)));
}

// 0x29e22: on the idle arm (|d| < 8 px, no vertical speed) the marble is thrown (TUBE with
// +0x58 the tag, the arm script runs); while the arm moves the marble keeps its old position.
// Not ported: two players (the partner waits on the arm).
void catapult(Mem &m, uint32_t a2, uint32_t s, int16_t d6, int16_t a0) {
  if (!within(d6, a0, -7, 8, -7, 8) || m.rl(a2 + P_VZ)) { return; }
  if (m.rl(s + ANIM) != m.rl(s + ANIM2) || m.rw(s + COUNT)) {
    m.wl(a2 + P_PX, m.rl(OLD_X));
    m.wl(a2 + P_PY, m.rl(OLD_Y));
    return;
  }

  m.wl(a2 + P_PX, m.rl(s + PX));
  m.wl(a2 + P_PY, m.rl(s + PY));
  m.wl(a2 + P_PZ, m.rl(a2 + P_PZ) - THROW_DZ);
  m.wl(a2 + P_VZ, THROW_VZ);
  m.wl(a2 + P_VX, (uint32_t)((int32_t)marble_rng::rng(m, THROW_VX) - (THROW_VX >> 1)));
  m.wl(a2 + P_VY, (uint32_t)(THROW_VY0 - (int32_t)marble_rng::rng(m, THROW_VY)));
  m.wb(a2 + P_AIR, AIRBORNE);  // sound 0x15884
  m.wb(a2 + P_STATE, marble_physics::TUBE);
  m.wb(a2 + P_SURFACE, m.rb(s + TAG));
  m.wb(a2 + P_SURFACE_T, THROW_TICKS);
  m.wl(s + PC, CATAPULT_SCRIPT);
  script_12896(m, s);
}

// gate frame (0x0b box; 0x0d transposed, ROM 0x2ae1a) below GATE_TOP: inside it before the
// move too (0x0b box for both, 0x2ae42): lost; else both axes bounce. The arcade's 0x0d path
// reads the old deltas from stale frame locals; computed here, as in the model.
void gateFrame(Mem &m, uint32_t a2, uint32_t s, uint8_t tag, int16_t d6, int16_t a0) {
  const int16_t fx = tag == TAG_GATE_B ? d6 : a0, fy = tag == TAG_GATE_B ? a0 : d6;
  if (!within(fx, fy, -0xc, 0x1c, -8, 0x10) || m.rw(POS_Z) >= GATE_TOP) { return; }

  int16_t pd6, pa0;
  oldDelta(m, s, pd6, pa0);
  if (within(pd6, pa0, -0xc, 0x1c, -8, 0x10) && m.rw(OLD_Z) < GATE_TOP) {
    m.wb(a2 + P_DAMAGE, EMBEDDED);
    marble_physics::enterState(m, a2, marble_physics::LOST);
    return;
  }
  m.wb(HIT_X, 1);
  m.wb(HIT_Y, 1);
}

// 0x2ab88 / 0x2ad20: while the gate sucks, its mouth pulls the marble to the centre (v += 4 x
// the unit vector); in the centre it is caught (CAUGHT, +0x58 the slot number); else the frame
void gate(Mem &m, uint32_t a2, uint32_t s, uint8_t tag, int16_t d6, int16_t a0) {
  const bool b = tag == TAG_GATE_B;
  if (!m.rb(s + STATE) || m.rl(s + ANIM2) != (b ? GATE_OPEN_B : GATE_OPEN_D)) { return; }

  const bool mouth = b ? within(d6, a0, -0xc, 0x1c, -0x20, -8) : within(d6, a0, -0x20, -8, -0xc, 0x1c);
  if (!mouth) {
    gateFrame(m, a2, s, tag, d6, a0);
    return;
  }
  const bool centre = b ? within(d6, a0, 0, 0x10, -0xf, -9) : within(d6, a0, -0xf, -9, 0, 0x10);
  if (centre) {
    m.wb(a2 + P_STATE, marble_physics::CAUGHT);
    m.wb(a2 + P_DAMAGE, CAUGHT_TICKS);
    m.wb(a2 + P_SURFACE, m.rb(s + IDX));
    return;
  }

  int16_t ux, uy;
  unit(b ? d6 - 8 : d6 + 0xc, b ? a0 + 0xc : a0 - 8, ux, uy);
  m.wl(a2 + P_VX, m.rl(a2 + P_VX) + (uint32_t)((int32_t)ux << 2));
  m.wl(a2 + P_VY, m.rl(a2 + P_VY) + (uint32_t)((int32_t)uy << 2));
}

// 0x2a9a2: the moving box's MO frame (bytes x, y, w, h at +4) widened by BOX_MARGIN: entering
// it bounces both axes; already inside: pushed away from its centre, dizzy (0x25c74, fall 0)
void bounceBox(Mem &m, uint32_t a2, uint32_t s, int16_t d6, int16_t a0) {
  if (!m.rb(s + STATE)) { return; }

  const uint32_t rec = gl(m, m.rl(s + ANIM));
  const int16_t x0 = s8(m.romb(rec + 4)), y0 = s8(m.romb(rec + 5));
  const int16_t x1 = x0 + s8(m.romb(rec + 6)), y1 = y0 + s8(m.romb(rec + 7));
  const int16_t lx = -(x1 + BOX_MARGIN), hx = -(x0 - BOX_MARGIN);
  const int16_t ly = -(y1 + BOX_MARGIN), hy = -(y0 - BOX_MARGIN);
  if (!within(d6, a0, lx, hx, ly, hy)) { return; }

  int16_t pd6, pa0;
  oldDelta(m, s, pd6, pa0);
  if (!within(pd6, pa0, lx, hx, ly, hy)) {
    m.wb(HIT_Y, 1);
    m.wb(HIT_X, 1);
    return;
  }

  static const int16_t STRAIGHT = 0x1000;  // distance 0: pushed +x
  const int16_t dx = d6 + 4, dy = a0 + 4;
  int16_t ux = STRAIGHT, uy = 0;
  if (distance(dx, dy)) { unit((int16_t)-dx, (int16_t)-dy, ux, uy); }
  m.wl(a2 + P_VX, (uint32_t)((int32_t)ux << 6));
  m.wl(a2 + P_VY, (uint32_t)((int32_t)uy << 6));
  if (m.rb(a2 + P_STATE) != marble_physics::DIZZY) {  // sound 0x39
    m.wb(a2 + P_ANIM_5F, 0);
    m.wb(a2 + P_ANIM_60, ANIM_RATE);
    m.wl(a2 + P_ANIM, ANIM_DIZZY);
  }
  m.wb(a2 + P_STATE, marble_physics::DIZZY);
  marble_physics::land(m, a2, 0);
  m.wb(a2 + P_DAMAGE, BOX_DAMAGE);
  m.wb(a2 + P_DIZZY_CNT, 0);
}

// one slot against object a2 (0x29cce loop body); true: leave 0x29cce without the epilogue
bool contact(Mem &m, uint32_t a2, uint32_t s, int32_t vx, int32_t vy) {
  const int16_t mcx = m.sw(CELL_X), mcy = m.sw(CELL_Y), mx = m.sw(POS_X), my = m.sw(POS_Y);
  const int16_t sx = m.sw(s + PX), sy = m.sw(s + PY);
  const int16_t d6 = s16((uint32_t)(sx - mx)), a0 = s16((uint32_t)(sy - my));
  const int16_t d1 = s16((uint32_t)((sx >> 3) - mcx)), d2 = s16((uint32_t)((sy >> 3) - mcy));
  const int8_t tag = s8(m.rb(s + TAG));
  if (tag < TAG_BOUNDS_LO || tag > TAG_BOUNDS_HI) { return false; }

  for (const Wall &w : WALLS) {
    if (w.tag != tag) { continue; }

    const int16_t al = w.alongX ? d6 : a0, pe = w.alongX ? a0 : d6;
    const int32_t v = w.alongX ? vx : vy;
    if (!(w.al0 < al && al < w.al1 && w.pe0 < pe && pe < w.pe1)) { return false; }

    if ((w.e0a < al && al < w.e0b && v < 0) || (w.e1a < al && al < w.e1b && v > 0)) {
      m.wb(w.alongX ? HIT_X : HIT_Y, 1);
    }
    m.wb(w.alongX ? HIT_Y : HIT_X, 1);  // sound 0x42
    return false;
  }

  for (const Rect &r : RECTS) {
    if (r.tag != tag) { continue; }

    if (0 <= d1 && d1 < r.w && 0 <= d2 && d2 < r.h &&
        (!r.sameZ || m.rl(s + PZ) == m.rl(a2 + P_PZ))) {
      setSurface(m, a2, tag);
    }
    return false;
  }

  const uint16_t z = m.rw(POS_Z);
  for (const TubeHole &h : TUBE_HOLES) {
    if (h.tag == tag) { return tubeHole(m, a2, s, h, d1, d2, z) != 0; }
  }
  uint8_t lost;
  if (tubeWall(m, a2, s, (uint8_t)tag, d1, d2, d6, a0, lost)) { return lost != 0; }
  if (tag == TAG_ENTRY_A || tag == TAG_ENTRY_B) { return tubeEntry(m, a2, s, d1, d2, d6, a0) != 0; }
  if (tag == TAG_HOLE_U) {
    tubeHoleU(m, a2, s, d1, d2, z);
    return false;
  }
  if (tag == TAG_FUNNEL) {
    funnel_22(m, a2, s, d1, d2, z);
    return false;
  }
  if (tag == TAG_CATAPULT) {
    catapult(m, a2, s, d6, a0);
    return false;
  }
  if (tag == TAG_CROWD) {
    bounceBox(m, a2, s, d6, a0);
    return false;
  }
  if (tag == TAG_GATE_B || tag == TAG_GATE_D) {
    gate(m, a2, s, (uint8_t)tag, d6, a0);
    return false;
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
  return false;
}

}  // namespace

// 0x29cce: collision with the level object slots. Ported: wall segments (0x1a-0x1f), round
// posts (0x05, Marble Love sub-29cce.ts), cell rectangles that set the surface code +0x58,
// the tubes 0x12-0x16 and 0x20-0x27, the aerial catapult 0x0a, gates 0x0b/0x0d and bounce
// boxes 0x0c.
void slots_29cce(Mem &m, uint32_t a2) {
  const int32_t vx = m.sl(a2 + P_VX), vy = m.sl(a2 + P_VY);
  m.wb(a2 + P_SURFACE, 0);

  for (uint8_t i = 0; i < SLOT_N; i++) {
    const uint32_t s = slotAddr(i);
    if (!m.rb(s + ACTIVE)) { continue; }

    if (contact(m, a2, s, vx, vy)) { return; }
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

// 0x144e4: camera move: band = (camera - descriptor top) >> 4. Enemy spawners (0x15a12,
// 0x14c46, 0x17346) before the slots, the silly race bird flag after them; the aerial
// extras and the silly minis are not ported.
void cameraBands(Mem &m, int16_t oldCam, int16_t newCam) {
  const int16_t top = (int16_t)m.romw(m.desc() + DESC_TOP);
  const int16_t a = (int16_t)(oldCam - top) >> 4, b = (int16_t)(newCam - top) >> 4;
  if ((a & 0xff) == (b & 0xff)) { return; }
  marble_enemies::cameraBands(m, a, b);  // 0x15a12 steelies before the slots
  spawn_12dfa(m, a, b);
  marble_enemies::sillyBands(m, a, b);
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

// gates (aerial): their scripts restart on zone hooks
static const uint32_t GATE_WAIT_B = 0x1d766, GATE_WAIT_D = 0x1d7ac;    // 0x13966

// 0x13966 slot part: zone wait slots of `zone` restart their script (gates from their wait
// script; sounds 0x1ef42 not ported)
void zoneScripts(Mem &m, uint8_t zone) {
  for (uint8_t i = 0; i < SLOT_N; i++) {
    const uint32_t a = slotAddr(i);
    if (m.rb(a + ACTIVE) != 1 || m.rb(a + STATE) != ST_ZONE || m.rb(a + ZONE) != zone) {
      continue;
    }
    if (m.rb(a + TAG) == TAG_GATE_B) { m.wl(a + PC, GATE_WAIT_B); }
    if (m.rb(a + TAG) == TAG_GATE_D) { m.wl(a + PC, GATE_WAIT_D); }
    script_12896(m, a);
  }
}

// 0x13892 (aerial zone 4 entry): zone wait slots removed, open gates (state 2) closed again
// (sounds 0x1ef5a not ported)
void zone4_13892(Mem &m) {
  for (uint8_t i = 0; i < SLOT_N; i++) {
    const uint32_t a = slotAddr(i);
    if (m.rb(a + ACTIVE) != 1) { continue; }
    if (m.rb(a + STATE) == ST_ZONE) {
      slot_start_12f44(m, a, SPAWN_REMOVE);
      continue;
    }
    const uint8_t tag = m.rb(a + TAG);
    if (m.rb(a + STATE) != ST_ANIM2 || (tag != TAG_GATE_B && tag != TAG_GATE_D)) { continue; }
    m.wl(a + PC, tag == TAG_GATE_B ? GATE_CLOSE_B : GATE_CLOSE_D);
    script_12896(m, a);
  }
}

// 0x2563e end: the gate that caught the marble closes (sound 0x1ef5a not ported)
void closeGate(Mem &m, uint8_t slot) {
  const uint32_t a = slotAddr(slot);
  m.wl(a + PC, m.rb(a + TAG) == TAG_GATE_B ? GATE_CLOSE_B : GATE_CLOSE_D);
  script_12896(m, a);
}

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
