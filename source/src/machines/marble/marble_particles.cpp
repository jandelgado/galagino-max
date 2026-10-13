#include "marble_particles.h"

#ifdef ENABLE_MARBLE

#include "marble_rng.h"

namespace marble_particles {

using marble_physics::Mem;
using marble_physics::s16;

static const uint32_t SLOTS = 0x400a9c, COUNT = 0x4003e2;
static const uint8_t STRIDE = 10;
static const uint8_t X = 0, Y = 2, VX = 4, VY = 6, MODE = 8;
static const uint8_t FILL_SETS = 8;
static const int16_t MIN_X = 8, MAX_X = 0x148, MIN_Y = 0, MAX_Y = 0xe0;  // pixels

// view position of a slot: MO x = x - 8 (left half), top line 224 - y (MARBLE.md "Attract"),
// the arcade screen 48 px left of and 24 lines above the view's
static const int16_t VIEW_DX = -8 - 48, VIEW_Y = 224 + 24;

// random velocity: rng(96) - 0x30, moved 0x10 away from 0
static uint16_t velocity(Mem &m) {
  const int16_t v = (int16_t)(marble_rng::rng(m, 0x60) - 0x30);
  return (uint16_t)(v >= 0 ? v + 0x10 : v - 0x10);
}

// 0x26cfa palette fill (rng(2) per colour set) then 0x18cd2
uint8_t init(Mem &m, uint8_t count, uint8_t mode) {
  uint8_t fill = 0;
  if (mode == FINAL_MODE) {
    for (uint8_t k = 0; k < FILL_SETS; k++) { fill |= (uint8_t)(marble_rng::rng(m, 2) << k); }
  }

  for (uint8_t i = 0; i < count; i++) {
    const uint32_t a = SLOTS + i * STRIDE;
    m.ww(a + X, (uint16_t)(((marble_rng::rng(m, 0x100) & 0xff) + 0x24) << 4));
    m.ww(a + Y, (uint16_t)((marble_rng::rng(m, 0x80) + 0x30) << 4));
    m.ww(a + VX, velocity(m));
    m.ww(a + VY, velocity(m));
    uint16_t c = mode;
    if (mode >= 0x80) { c = marble_rng::rng(m, mode == FINAL_MODE ? 8 : 2); }
    m.ww(a + MODE, (uint16_t)(c << 11));
  }
  m.wb(COUNT, count);
  return fill;
}

// 0x18dca: reflect at the edges, move only while inside
void bounce(Mem &m) {
  const uint8_t n = m.rb(COUNT);
  for (uint8_t i = 0; i < n; i++) {
    const uint32_t a = SLOTS + i * STRIDE;
    const int16_t vx = m.sw(a + VX), vy = m.sw(a + VY);
    const int16_t nx = (int16_t)(m.sw(a + X) + vx), ny = (int16_t)(m.sw(a + Y) + vy);
    const int16_t px = nx >> 4, py = ny >> 4;

    if ((px < MIN_X && vx < 0) || (px > MAX_X && vx > 0)) { m.ww(a + VX, (uint16_t)-vx); }
    if ((py < MIN_Y && vy < 0) || (py > MAX_Y && vy > 0)) { m.ww(a + VY, (uint16_t)-vy); }
    if (px < MIN_X || px > MAX_X || py < MIN_Y || py > MAX_Y) { continue; }

    m.ww(a + X, (uint16_t)nx);
    m.ww(a + Y, (uint16_t)ny);
  }
}

void publish(const Mem &m, uint8_t mode, uint8_t fill, marble_video::VideoState &v) {
  uint8_t n = m.rb(COUNT);
  if (n > marble_video::PARTS) { n = marble_video::PARTS; }
  for (uint8_t i = 0; i < n; i++) {
    const uint32_t a = SLOTS + i * STRIDE;
    v.parts[i].x = (int16_t)((m.sw(a + X) >> 4) + VIEW_DX);
    v.parts[i].y = (int16_t)(VIEW_Y - (m.sw(a + Y) >> 4));
    v.parts[i].colour = (uint8_t)(m.rw(a + MODE) >> 11);
  }
  v.nparts = n;
  v.n = 0;
  v.partMode = mode;
  v.partFill = fill;
}

void clear(Mem &m) { m.wb(COUNT, 0); }

}  // namespace marble_particles

#endif  // ENABLE_MARBLE
