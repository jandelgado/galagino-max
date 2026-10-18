#ifndef DISCRETE_H
#define DISCRETE_H

#include <stdint.h>

// Building blocks for MAME style discrete sound circuits rendered at
// 24 kHz (Asteroids, Lunar Lander).
namespace discrete {

static const float SAMPLE_RATE = 24000.0f;
static const float DT = 1.0f / SAMPLE_RATE;

// first order low pass, y += a * (x - y)
struct LowPass {
  float a, y;
  void set(float fc);
  float step(float x) { return y += a * (x - y); }
};

// MAME dst_filter2 band pass (bilinear, pre-warped)
struct BandPass {
  float b0, a1, a2, x1, x2, y1, y2;
  void set(float fc, float damp);
  float step(float x);
};

// 16 bit LFSR, XNOR of bits 6 and 14 (MAME asteroid/llander lfsr),
// clocked at 12 kHz = every 2nd sample, +-0.5. A changed reset_count
// (count of noise reset writes) clears it.
struct Noise {
  uint16_t lfsr;
  uint8_t reset_count, div;
  float value;
  void reset();
  float step(uint8_t reset_count);
  bool ticked() const { return div == 0; } // step() just clocked
};

} // namespace discrete

#endif
