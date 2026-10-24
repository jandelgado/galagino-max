#ifndef BWIDOW_INPUTS_H
#define BWIDOW_INPUTS_H

#include <stdint.h>
#include "../../emulation/twin_stick.h"

// Black Widow reads its move stick at IN3 (0x8000) and its fire stick plus
// START1 at IN4 (0x8800), active low (MAME INPUT_PORTS_START(bwidow)). Both
// sticks come from TwinStick: one stick + FIRE (strafe lock), or
// controller X/B/A/Y to fire up/right/down/left.
enum : uint8_t { BW_RIGHT = 0x01, BW_LEFT = 0x02, BW_DOWN = 0x04, BW_UP = 0x08, BW_START1 = 0x20 };

static inline uint8_t bwidow_stick(bool up, bool down, bool left, bool right) {
  uint8_t v = 0xff;
  if (up) {
    v &= ~BW_UP;
  }
  if (down) {
    v &= ~BW_DOWN;
  }
  if (left) {
    v &= ~BW_LEFT;
  }
  if (right) {
    v &= ~BW_RIGHT;
  }
  return v;
}

// s = TwinStick::update() result
static inline uint8_t bwidow_in3(uint16_t s) {
  return bwidow_stick(s & TwinStick::MOVE_UP, s & TwinStick::MOVE_DOWN,
                      s & TwinStick::MOVE_LEFT, s & TwinStick::MOVE_RIGHT);
}

// k = buttons
static inline uint8_t bwidow_in4(uint16_t s, unsigned int k) {
  const uint8_t v = bwidow_stick(s & TwinStick::FIRE_UP, s & TwinStick::FIRE_DOWN,
                                 s & TwinStick::FIRE_LEFT, s & TwinStick::FIRE_RIGHT);
  return (k & BOARD_KEY_START) ? uint8_t(v & ~BW_START1) : v;
}

#endif
