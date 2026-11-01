#ifndef HAL_GAMEPAD_H
#define HAL_GAMEPAD_H

// hal::Gamepad: a Bluetooth pad as a generic gamepad (no galagino buttons;
// input/bt_gamepad maps them). Not part of hal.h: only Input includes it.

#include <stdint.h>

namespace hal {

struct GamepadState {
  enum : uint8_t { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8 };
  enum : uint16_t {
    A = 0x001, B = 0x002, X = 0x004, Y = 0x008, L1 = 0x010, R1 = 0x020, L2 = 0x040, R2 = 0x080,
    SELECT = 0x100, START = 0x200, SYSTEM = 0x400
  };
  // stick range, y negative is up
  enum : int16_t { STICK_MIN = -512, STICK_MAX = 511 };

  bool connected;
  uint8_t dpad;      // UP|DOWN|LEFT|RIGHT
  int16_t x, y;      // left stick
  uint16_t buttons;  // A..SYSTEM
};

} // namespace hal

#if !defined(HAL_GAMEPAD_BT)
#error "hal/gamepad.h needs HAL_GAMEPAD_BT"
#elif defined(HAL_RP2350)
#include "rp2350/gamepad.h"
#elif defined(HAL_HOST)
#include "host/gamepad.h"
#else
#error "no hal::Gamepad backend for this platform"
#endif

#endif // HAL_GAMEPAD_H
