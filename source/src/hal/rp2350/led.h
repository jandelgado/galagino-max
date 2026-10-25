#ifndef HAL_RP2350_LED_H
#define HAL_RP2350_LED_H

#include <stdint.h>

namespace hal {

// No LED strip on this backend yet.
class LedStrip {
public:
  static void init(int, uint32_t) {}
  static void show(const uint8_t *, uint32_t) {}
};

} // namespace hal

#endif // HAL_RP2350_LED_H
