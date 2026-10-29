#ifndef HAL_HOST_GAMEPAD_H
#define HAL_HOST_GAMEPAD_H

namespace hal {
namespace host {

inline GamepadState &_gamepad() {
  static GamepadState state;
  return state;
}

// Test control of the pad state read() returns.
inline void setGamepad(const GamepadState &state) { _gamepad() = state; }

} // namespace host

class Gamepad {
public:
  static void begin() {}
  static void read(GamepadState &out) { out = host::_gamepad(); }
};

} // namespace hal

#endif // HAL_HOST_GAMEPAD_H
