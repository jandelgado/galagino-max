#ifndef HAL_ESP_LED_H
#define HAL_ESP_LED_H

#include <stdint.h>

namespace hal {

// WS2812 strip. rgb is 3 bytes per LED (r, g, b) before brightness scaling;
// the backend applies LED_BRIGHTNESS and the strip's color order.
// Implemented by esp/led_strip.cpp (RMT).
class LedStrip {
public:
  static void init(int pin, uint32_t count);
  static void show(const uint8_t *rgb, uint32_t count);
};

} // namespace hal

#endif // HAL_ESP_LED_H
