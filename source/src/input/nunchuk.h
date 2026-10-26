#ifndef INPUT_NUNCHUK_H
#define INPUT_NUNCHUK_H

#include "../config.h"

#ifdef NUNCHUCK_INPUT
#include <stdint.h>

// Nintendo Nunchuk on I2C port 0, unencrypted mode. Stick beyond
// NUNCHUCK_MOVE_THRESHOLD = direction, Z = FIRE, C = EXTRA.
class Nunchuk {
public:
  void setup();
  void enable();
  void disable();
  uint16_t getInput();
  // Address acknowledges (a real I2C transfer, not cached).
  bool connected();

private:
  bool enabled = false;
  uint32_t lastPoll = 0;
  uint16_t lastValue = 0;
};

#endif

#endif // INPUT_NUNCHUK_H
