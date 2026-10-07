#ifndef STARWARS_YOKE_H
#define STARWARS_YOKE_H

#include <stdint.h>

// One axis of the Star Wars flight yoke (ADC 0x00-0xff, 0x80 centered)
// driven by the digital stick, like MAME's keyboard analog with
// autocenter off: holding a direction moves the crosshair, accelerating
// for fine aiming first; releasing keeps it there. Reversing stops at a
// center detent; release and press again to move past it.
struct YokeAxis {
  static const uint8_t CENTER = 0x80;
  static const uint8_t SPEED_MIN = 2, SPEED_MAX = 8; // per frame

  uint8_t pos = CENTER;
  uint8_t speed = SPEED_MIN;
  bool detent = false; // stopped at center, waits for release

  // dir: -1 toward 0x00, 0 released, 1 toward 0xff; once per 60 Hz frame
  void step(int8_t dir) {
    if (!dir) {
      speed = SPEED_MIN;
      detent = false;
      return;
    }

    if (detent) {
      return;
    }

    const int16_t p = pos + dir * speed;
    if ((pos < CENTER && p >= CENTER) || (pos > CENTER && p <= CENTER)) {
      pos = CENTER;
      detent = true;
      return;
    }

    pos = p < 0 ? 0 : (p > 0xff ? 0xff : p);
    if (speed < SPEED_MAX) {
      speed++;
    }
  }
};

#endif
