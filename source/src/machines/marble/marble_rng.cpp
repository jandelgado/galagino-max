#include "marble_rng.h"

#ifdef ENABLE_MARBLE

namespace marble_rng {

static const uint32_t STATE = 0x4003a6;  // word
static const uint8_t NO_FEEDBACK = 0x40;  // feedback byte 0 counts as bit 6 set

// 0x13a98 (marble_sim.py rng_13a98)
uint16_t rng(marble_physics::Mem &m, uint16_t n) {
  uint16_t d0 = m.rw(STATE);
  uint16_t d1 = n;
  uint8_t k = 0;
  do {
    uint8_t d2 = (uint8_t)((d0 >> 8) ^ d0);
    if (d2 == 0) { d2 = NO_FEEDBACK; }
    d0 = (uint16_t)((d0 << 1) | ((d2 >> 6) & 1));  // asl.b #2 -> X = bit 6, roxl.w
    k++;
    d1 >>= 1;
  } while (d1);
  m.ww(STATE, d0);
  if (n == 0) { return d0; }

  d0 &= (uint16_t)((1u << k) - 1);
  while (!((int16_t)n > (int16_t)d0)) { d0 = (uint16_t)(d0 - n); }  // cmp.w, bgt: signed
  return d0;
}

}  // namespace marble_rng

#endif  // ENABLE_MARBLE
