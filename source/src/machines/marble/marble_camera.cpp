#include "marble_camera.h"

#ifdef ENABLE_MARBLE

#include "marble_physics.h"
#include "marble_video.h"

namespace marble_camera {

using marble_physics::Mem;
using marble_physics::FRAMES;
using marble_physics::RACE;
using marble_physics::CAMERA;
using marble_physics::CAMERA_L;
using marble_physics::DESC_TOP;
using marble_physics::DESC_RANGE;
using marble_physics::s16;

namespace {

// work RAM of 0x13ee6
static const uint32_t PF_SCROLL = 0x400000;  // playfield y scroll, mod 0x200
static const uint32_t DIR = 0x400004;        // 1 forward, 0xff back, 0 none
static const uint32_t ROW_DUE = 0x400006;    // a playfield row must be decoded
static const uint32_t SCROLLING = 0x400008;
static const uint32_t SPEED = 0x40000a;      // px per call
static const uint32_t GHOST_SY = 0x40000c;   // sy that keeps a scroll going without a player

// constants 0x1f1ca: speeds (bytes), sy thresholds and margins (words)
static const uint32_t SLOW = 0x1f1ca, MID = 0x1f1cc, FAST = 0x1f1ce;
static const uint32_t LOW = 0x1f1d0, HIGH = 0x1f1d2, NEAR = 0x1f1d4, FAR = 0x1f1d6;

// player object fields (MARBLE.md "Marble physics")
static const uint8_t SLOT = 0x18, STATE = 0x1a, SY = 0x20, AIR = 0x36, VZ = 0x08;
static const uint8_t AIRBORNE = 2;

static const uint8_t RACE_SILLY = 4;  // scrolls upward
static const uint8_t FWD = 1, BACK = 0xff;
static const int16_t STOP_MARGIN = 0x10;

// X pan: view x window and step thresholds (same shape as the Y rule)
static const int16_t PAN_LO = 64, PAN_HI = 176, PAN_MID = 16, PAN_FAR = 48;

inline int16_t rom(const Mem &m, uint32_t a) { return (int16_t)m.romw(a); }

// lowest sy of the players that steer the camera (silly: highest); only player 1 here
int16_t playerSy(const Mem &m, int16_t none, bool silly) {
  const uint32_t a0 = marble_physics::PLAYER;
  const int16_t st = (int8_t)m.rb(a0 + STATE);
  if (m.rb(a0 + SLOT) != 1) { return none; }
  if (m.rb(a0 + AIR) == AIRBORNE && m.sl(a0 + VZ) <= 0) { return none; }  // falling
  if (st != 0 && st != 1 && st != 3 && st != 4 && st != 5) { return none; }

  const int16_t sy = m.sw(a0 + SY);
  return (silly ? sy > none : sy < none) ? sy : none;
}

// a scroll without a player (falling): continue as if sy sat where this speed starts
int16_t ghostSy(Mem &m) {
  if (m.rw(GHOST_SY) != 0) { return m.sw(GHOST_SY); }

  const uint8_t speed = m.rb(SPEED);
  const bool fwd = m.rb(DIR) == FWD;
  int32_t v;
  if (speed == m.romb(FAST)) {
    v = fwd ? (int32_t)rom(m, LOW) - rom(m, FAR) : (int32_t)rom(m, HIGH) + rom(m, FAR);
  } else if (speed == m.romb(MID)) {
    v = fwd ? (int32_t)rom(m, LOW) - rom(m, NEAR) : (int32_t)rom(m, HIGH) + rom(m, NEAR);
  } else {
    v = fwd ? rom(m, LOW) : rom(m, HIGH);
  }
  m.ww(GHOST_SY, (uint16_t)v);
  return m.sw(GHOST_SY);
}

}  // namespace

int16_t top(const Mem &m) { return s16(m.rw(CAMERA) - m.romw(m.desc() + DESC_TOP)); }

int16_t range(const Mem &m) { return rom(m, m.desc() + DESC_RANGE); }

void start(Mem &m, uint8_t race) {
  int32_t cam = rom(m, m.desc() + DESC_TOP);
  if (race == RACE_SILLY) { cam += range(m); }
  m.wl(CAMERA_L, (uint32_t)cam);

  m.ww(PF_SCROLL, 0);
  m.wb(DIR, 0);
  m.wb(ROW_DUE, 0);
  m.wb(SCROLLING, 0);
  m.wb(SPEED, 0);
  m.ww(GHOST_SY, 0);
}

// 0x13ee6 from 0x1411c. Not ported: row decode (the renderer reads the whole level). The
// caller runs the band spawns 0x144e4 (marble_objects::cameraBands) on a changed camera word.
void tick(Mem &m) {
  m.wb(ROW_DUE, 0);  // 0x13f06 decodes the row here; the renderer reads the whole level
  if ((m.rl(FRAMES) & 7) && !m.rb(SCROLLING)) { return; }

  const bool silly = m.rw(RACE) == RACE_SILLY;
  const int16_t none = silly ? s16(0x9000) : 0x7000;
  int16_t d3 = playerSy(m, none, silly);
  if (d3 == none && !m.rb(SCROLLING)) { return; }
  if (d3 != none) {
    m.ww(GHOST_SY, 0);
  } else {
    d3 = ghostSy(m);
  }

  const int16_t low = rom(m, LOW), high = rom(m, HIGH), near = rom(m, NEAR), far = rom(m, FAR);
  const int16_t camTop = rom(m, m.desc() + DESC_TOP);
  const uint8_t dir = m.rb(DIR);
  const bool scrolling = m.rb(SCROLLING);
  int16_t pf = m.sw(PF_SCROLL), cam = m.sw(CAMERA);
  uint8_t newDir = 0;

  if ((d3 < low && d3 != none) || (scrolling && dir == FWD)) {
    // forward: speed by how far sy is below the low threshold
    if (d3 < low - far) {
      m.wb(SPEED, m.romb(FAST));
    } else if (d3 < low - near) {
      m.wb(SPEED, m.romb(MID));
    } else if (d3 > low) {
      m.wb(SPEED, m.romb(SLOW));
    }
    const int8_t step = m.sb(SPEED);
    pf += step;
    if (pf > 0x1ff) { pf -= 0x200; }
    cam += step;
    newDir = FWD;
    if (cam > (int32_t)range(m) + camTop) {
      m.wb(SCROLLING, 0);
      cam = m.sw(CAMERA);
      pf = m.sw(PF_SCROLL);
    } else if ((m.sw(PF_SCROLL) >> 3) != (pf >> 3)) {
      m.wb(ROW_DUE, 1);
    }
  } else if ((d3 > high && d3 != none) || (scrolling && dir == BACK)) {
    // back: speed by how far sy is above the high threshold
    if (d3 > far + high) {
      m.wb(SPEED, m.romb(FAST));
    } else if (d3 > near + high) {
      m.wb(SPEED, m.romb(MID));
    } else if (d3 < high) {
      m.wb(SPEED, m.romb(SLOW));
    }
    const int8_t step = m.sb(SPEED);
    pf -= step;
    if (pf < 0) { pf += 0x200; }
    cam -= step;
    newDir = BACK;
    if (cam < camTop) {
      m.wb(SCROLLING, 0);
      cam = m.sw(CAMERA);
      pf = m.sw(PF_SCROLL);
    } else if ((m.sw(PF_SCROLL) >> 3) != (pf >> 3)) {
      m.wb(ROW_DUE, 1);
    }
  }

  if ((int32_t)cam != m.sl(CAMERA_L)) {
    m.ww(PF_SCROLL, (uint16_t)pf);
    m.wl(CAMERA_L, (uint32_t)(int32_t)cam);
    if (m.rw(GHOST_SY) != 0) {
      const int8_t step = m.sb(SPEED);
      m.ww(GHOST_SY, (uint16_t)(m.rb(DIR) == FWD ? m.sw(GHOST_SY) + step : m.sw(GHOST_SY) - step));
    }
    m.wb(SCROLLING, 1);
    m.wb(DIR, newDir);
  }

  // stop once sy is back inside the low/high band (with margin)
  if (d3 == none) { return; }
  if (d3 > low + STOP_MARGIN && m.rb(DIR) == FWD) {
    m.wb(SCROLLING, 0);
  } else if (d3 < high - STOP_MARGIN && m.rb(DIR) == BACK) {
    m.wb(SCROLLING, 0);
  }
}

int16_t pan(int16_t p, int16_t x) {
  const int16_t vx = x - p;
  int16_t d = 0;
  if (vx < PAN_LO) { d = vx - PAN_LO; }
  if (vx > PAN_HI) { d = vx - PAN_HI; }
  if (d == 0) { return p; }

  const int16_t a = d < 0 ? -d : d;
  const int16_t step = a > PAN_FAR ? 4 : a > PAN_MID ? 2 : 1;
  p += d < 0 ? -step : step;
  if (p < 0) { p = 0; }
  if (p > marble_video::PAN) { p = marble_video::PAN; }
  return p;
}

}  // namespace marble_camera

#endif  // ENABLE_MARBLE
