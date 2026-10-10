#ifndef TEMPEST_SPINNER_H
#define TEMPEST_SPINNER_H

#include <stdint.h>

// Tempest spinner (4 bit counter read through POKEY 1) from the digital
// stick: holding left/right turns the knob, accelerating. Called once per
// 60 Hz frame. The game reads the signed 4 bit difference per IRQ, so a
// frame moves at most 7 counts (72 counts per knob turn).
class Spinner {
public:
  // dir: -1 left, 0 released, 1 right
  void step(int8_t dir) {
    if (dir != last_dir) {
      speed = SPEED_MIN;
      last_dir = dir;
    }
    if (!dir) {
      return;
    }

    pos += dir * speed;
    if (speed < SPEED_MAX) {
      speed += ACCEL;
    }
  }

  uint8_t count() const { return (pos >> FRAC_BITS) & 0x0f; }

private:
  // speeds in quarter counts per frame: 0.5 .. 6 counts
  static const uint8_t FRAC_BITS = 2;
  static const uint8_t SPEED_MIN = 2, SPEED_MAX = 24, ACCEL = 1;

  uint8_t pos = 0; // quarter counts; wraps at 64 counts, a multiple of 16
  uint8_t speed = SPEED_MIN;
  int8_t last_dir = 0;
};

#endif
