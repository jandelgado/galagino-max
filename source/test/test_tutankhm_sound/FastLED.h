#pragma once
// host stub: machineBase.h includes FastLED.h when config_local.h sets LED_PIN
#include <stdint.h>

struct CRGB {
  enum : uint32_t { Black = 0, Red = 0xff0000, Green = 0x00ff00, Blue = 0x0000ff,
                    Yellow = 0xffff00, Magenta = 0xff00ff, Cyan = 0x00ffff, White = 0xffffff };
  uint8_t r, g, b;
  CRGB() : r(0), g(0), b(0) {}
  CRGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
  CRGB(uint32_t c) : r(c >> 16), g(c >> 8), b(c) {}
};

inline void fill_solid(CRGB *leds, int n, const CRGB &c) {
  for (int i = 0; i < n; i++) {
    leds[i] = c;
  }
}
