#ifndef DC_BLOCK_H
#define DC_BLOCK_H

#include <stdint.h>

// DC blocker: one-pole high-pass (~30 Hz at 24 kHz) for DACs that idle at
// an arbitrary level. Level in Q8: a plain (s - level) >> 7 rounds toward
// -inf and settles up to 127 below the input.
class DcBlock {
public:
  int32_t filter(int32_t s) {
    level += (s * ONE - level) >> 7;
    return s - level / ONE;
  }

private:
  static const int32_t ONE = 256;
  int32_t level = 0;
};

// Integer DC tracker without fraction bits: level += (s - level) >> 7.
// Rounds toward -inf and settles up to 127 below a constant input; kept
// for the gyruss and circusc drum DACs as tuned on hardware.
class DcTrack {
public:
  int32_t filter(int32_t s) {
    level += (s - level) >> 7;
    return s - level;
  }

private:
  int32_t level = 0;
};

#endif
