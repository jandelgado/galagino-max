#ifndef TWIN_STICK_H
#define TWIN_STICK_H

#include <stdint.h>
#include "board_keys.h"

// Twin sticks on one stick + FIRE (strafe lock): a FIRE press
// aims where the stick points (or the last move if centered); while held,
// that aim stays and the stick only moves. Controller X/B/A/Y aim
// up/right/down/left directly.
class TwinStick {
public:
  // TwinStick's own bit values; each machine maps them to its ports
  static const uint16_t MOVE_UP = 0x01, MOVE_DOWN = 0x02, MOVE_LEFT = 0x04, MOVE_RIGHT = 0x08;
  static const uint16_t FIRE_UP = 0x40, FIRE_DOWN = 0x80, FIRE_LEFT = 0x100, FIRE_RIGHT = 0x200;

  uint16_t update(unsigned int keys) {
    const uint8_t dir = stick(keys);
    if (dir) {
      lastMove = dir;
    }
    const bool fire = keys & BOARD_KEY_FIRE;
    if (fire && !fireLast) {
      aim = dir ? dir : lastMove;
    }
    fireLast = fire;

    uint16_t v = dir;  // same bit order as IN0 b0-3
    if (fire) {
      v |= fireBits(aim);
    }
    return v | buttonAim(keys);
  }

private:
  static const uint8_t UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8;

  static uint8_t stick(unsigned int k) {
    return ((k & BOARD_KEY_UP) ? UP : 0) | ((k & BOARD_KEY_DOWN) ? DOWN : 0) |
           ((k & BOARD_KEY_LEFT) ? LEFT : 0) | ((k & BOARD_KEY_RIGHT) ? RIGHT : 0);
  }

  static uint16_t fireBits(uint8_t d) {
    return ((d & UP) ? FIRE_UP : 0) | ((d & DOWN) ? FIRE_DOWN : 0) |
           ((d & LEFT) ? FIRE_LEFT : 0) | ((d & RIGHT) ? FIRE_RIGHT : 0);
  }

  static uint16_t buttonAim(unsigned int k) {
    return ((k & BOARD_KEY_X) ? FIRE_UP : 0) | ((k & BOARD_KEY_A) ? FIRE_DOWN : 0) |
           ((k & BOARD_KEY_Y) ? FIRE_LEFT : 0) | ((k & BOARD_KEY_B) ? FIRE_RIGHT : 0);
  }

  uint8_t lastMove = UP, aim = UP;
  bool fireLast = false;
};

#endif
