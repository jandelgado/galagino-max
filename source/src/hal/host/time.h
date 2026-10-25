#ifndef HAL_HOST_TIME_H
#define HAL_HOST_TIME_H

#include <stdint.h>

namespace hal {
namespace host {

inline uint64_t &_clockUs() {
  static uint64_t us = 0;
  return us;
}

// Test control of the 64 bit microsecond clock.
inline void setMicros(uint64_t us) { _clockUs() = us; }
inline void advanceMicros(uint64_t us) { _clockUs() += us; }

} // namespace host

inline uint32_t micros() { return (uint32_t)host::_clockUs(); }
inline uint32_t millis() { return (uint32_t)(host::_clockUs() / 1000); }

} // namespace hal

#endif // HAL_HOST_TIME_H
