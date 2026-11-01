#ifndef HAL_HOST_GPIO_H
#define HAL_HOST_GPIO_H

#include <stdint.h>

namespace hal {
namespace host {

static const int PIN_COUNT = 64;

inline bool *_pinLevels() {
  static bool levels[PIN_COUNT];
  return levels;
}

// Test control of an input pin.
inline void setPinLevel(int pin, bool level) { _pinLevels()[pin] = level; }

} // namespace host

inline void pinOutput(int pin, bool level) { host::_pinLevels()[pin] = level; }
inline void pinInputPullup(int pin) { host::_pinLevels()[pin] = true; }
inline void pinWrite(int pin, bool level) { host::_pinLevels()[pin] = level; }
inline bool pinRead(int pin) { return host::_pinLevels()[pin]; }

} // namespace hal

#endif // HAL_HOST_GPIO_H
