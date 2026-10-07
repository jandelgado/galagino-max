#ifndef STARWARS_YOKE_H
#define STARWARS_YOKE_H

#include <stdint.h>

// One axis of the Star Wars flight yoke (ADC 0x00-0xff, 0x80 centered)
// driven by the digital stick: holding a direction accelerates the
// crosshair for fine aiming first, releasing springs it back to center
// like the real yoke.
struct YokeAxis {
  static const uint8_t CENTER = 0x80;
  static const uint8_t SPEED_MIN = 2, SPEED_MAX = 8; // per frame
  static const uint8_t SPRING = 8;                    // return per frame

  uint8_t pos = CENTER;
  uint8_t speed = SPEED_MIN;

  // dir: -1 toward 0x00, 0 released, 1 toward 0xff; once per 60 Hz frame
  void step(int8_t dir) {
    if (!dir) {
      speed = SPEED_MIN;
      const int16_t d = (int16_t)CENTER - pos;
      pos += d > SPRING ? SPRING : (d < -SPRING ? -SPRING : d);
      return;
    }

    const int16_t p = pos + dir * speed;
    pos = p < 0 ? 0 : (p > 0xff ? 0xff : p);
    if (speed < SPEED_MAX) {
      speed++;
    }
  }
};

#endif
