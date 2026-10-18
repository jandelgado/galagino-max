#include "discrete.h"
#include <math.h>

namespace discrete {

void LowPass::set(float fc) {
  a = 1.0f - expf(-2.0f * (float)M_PI * fc * DT);
  y = 0;
}

void BandPass::set(float fc, float damp) {
  const float two_over_t = 2.0f * SAMPLE_RATE;
  const float wc = SAMPLE_RATE * 2.0f * tanf((float)M_PI * fc / SAMPLE_RATE);
  const float den = two_over_t * two_over_t + damp * wc * two_over_t + wc * wc;
  a1 = 2.0f * (-two_over_t * two_over_t + wc * wc) / den;
  a2 = (two_over_t * two_over_t - damp * wc * two_over_t + wc * wc) / den;
  b0 = damp * wc * two_over_t / den;
  x1 = x2 = y1 = y2 = 0;
}

float BandPass::step(float x) {
  const float y = -a1 * y1 - a2 * y2 + b0 * (x - x2);
  x2 = x1;
  x1 = x;
  y2 = y1;
  y1 = y;
  return y;
}

void Noise::reset() {
  lfsr = 0;
  reset_count = 0;
  div = 0;
  value = 0;
}

float Noise::step(uint8_t count) {
  static const uint8_t DIV = 2; // 12 kHz
  if (count != reset_count) {
    reset_count = count;
    lfsr = 0;
  }
  if (++div < DIV) {
    return value;
  }

  div = 0;
  const uint16_t fb = !(((lfsr >> 6) ^ (lfsr >> 14)) & 1);
  lfsr = (lfsr << 1) | fb;
  value = fb ? 0.5f : -0.5f;
  return value;
}

} // namespace discrete
