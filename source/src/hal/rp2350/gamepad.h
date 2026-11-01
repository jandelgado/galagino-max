#ifndef HAL_RP2350_GAMEPAD_H
#define HAL_RP2350_GAMEPAD_H

namespace hal {

// Bluetooth pad over the Pico 2 W's CYW43: BTstack + Bluepad32 in their own
// task on the video core (gamepad_bt.cpp).
class Gamepad {
public:
  static void begin();
  static void read(GamepadState &out);
};

} // namespace hal

#endif // HAL_RP2350_GAMEPAD_H
