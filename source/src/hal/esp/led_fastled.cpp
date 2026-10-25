// FastLED backend. Without LED_PIN the strip is absent and both calls do
// nothing. FastLED.h must not meet hal/led_color.h (both define CRGB).
#ifdef ARDUINO
#include "led.h"
#include "../../config.h"

#ifdef LED_PIN
#include <FastLED.h>
#include <string.h>

namespace hal {

// Static array, no heap: strips are a handful of LEDs.
static const uint32_t MAX_LEDS = 64;
static CRGB strip[MAX_LEDS];
static uint32_t stripCount = 0;

// FastLED needs the pin as a template argument: LED_PIN, not the parameter.
void LedStrip::init(int, uint32_t count) {
  if (!stripCount) {
    stripCount = count > MAX_LEDS ? MAX_LEDS : count;
    FastLED.addLeds<NEOPIXEL, LED_PIN>(strip, stripCount);
    FastLED.setBrightness(LED_BRIGHTNESS);
  }
}

void LedStrip::show(const uint8_t *rgb, uint32_t count) {
  if (!stripCount) {
    return;
  }
  memcpy(strip, rgb, (count > stripCount ? stripCount : count) * 3);
  FastLED.show();
}

} // namespace hal

#else

namespace hal {

void LedStrip::init(int, uint32_t) {}
void LedStrip::show(const uint8_t *, uint32_t) {}

} // namespace hal

#endif
#endif // ARDUINO
