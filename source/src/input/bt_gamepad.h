#ifndef INPUT_BT_GAMEPAD_H
#define INPUT_BT_GAMEPAD_H

#include "../config.h"

#ifdef HAL_GAMEPAD_BT
#include <stdint.h>
#include "../hal/gamepad.h"

// Bluetooth pad as BUTTON_* bits: dpad or stick = directions, A = FIRE (and
// BUTTON_A), B/X/Y/L1/R1 = their BUTTON_*, START, SELECT = COIN,
// SYSTEM (Home) = MENU. Disconnected: 0.
uint16_t btGamepadButtons(const hal::GamepadState &s);
// The pad's current bits.
uint16_t btGamepadInput();

#endif

#endif // INPUT_BT_GAMEPAD_H
