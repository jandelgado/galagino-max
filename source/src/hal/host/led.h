#ifndef HAL_HOST_LED_H
#define HAL_HOST_LED_H

#include <stdint.h>
#include <string.h>

namespace hal {
namespace host {

static const uint32_t LED_MAX = 64;

struct LedState {
  uint8_t frame[LED_MAX * 3];
  uint32_t count;
};

inline LedState &_ledState() {
  static LedState s;
  return s;
}

// Test access to what the last LedStrip::show() was given.
inline uint32_t ledCount() { return _ledState().count; }
inline const uint8_t *ledFrame() { return _ledState().frame; }

} // namespace host

class LedStrip {
public:
  static void init(int, uint32_t) {}
  static void show(const uint8_t *rgb, uint32_t count) {
    host::LedState &s = host::_ledState();
    s.count = count > host::LED_MAX ? host::LED_MAX : count;
    memcpy(s.frame, rgb, s.count * 3);
  }
};

} // namespace hal

#endif // HAL_HOST_LED_H
