#ifndef SPINNER_H
#define SPINNER_H

#include <stdint.h>

// Knob (spinner, paddle encoder) from the digital stick: holding
// left/right turns it, accelerating. Called once per 60 Hz frame. Speeds
// in quarter counts per frame; each game picks the range its counter
// tolerates (Tempest reads a signed 4 bit difference, Arkanoid 8 bit).
class Spinner {
public:
  Spinner(uint8_t speed_min, uint8_t speed_max, uint8_t accel)
      : speed_min(speed_min), speed_max(speed_max), accel(accel), speed(speed_min) {}

  // dir: -1 left, 0 released, 1 right
  void step(int8_t dir) {
    if (dir != last_dir) {
      speed = speed_min;
      last_dir = dir;
    }
    if (!dir) {
      return;
    }

    pos += dir * speed;
    if (speed < speed_max) {
      speed += accel;
    }
  }

  // low 8 bits of the counter; narrower counters mask it
  uint8_t count() const { return (uint8_t)(pos >> FRAC_BITS); }

private:
  static const uint8_t FRAC_BITS = 2;

  uint8_t speed_min, speed_max, accel;
  uint16_t pos = 0; // quarter counts; wraps at 16384 counts, a multiple of 256
  uint8_t speed;
  int8_t last_dir = 0;
};

#endif
