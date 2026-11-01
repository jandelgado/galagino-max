#ifndef HAL_LED_COLOR_H
#define HAL_LED_COLOR_H

// Portable CRGB: the subset of FastLED's CRGB the machines use, with the same
// names, values and rounding, so machine LED code does not know which LED
// backend is built. Defined in namespace hal and made visible as ::CRGB.

#include <stdint.h>

namespace hal {

struct CRGB {
  union {
    struct {
      uint8_t r;
      uint8_t g;
      uint8_t b;
    };
    uint8_t raw[3];
  };

  // HTML color codes, FastLED values (Green is 0x008000, not 0x00FF00)
  enum HTMLColorCode : uint32_t {
    Black = 0x000000,
    Blue = 0x0000FF,
    Cyan = 0x00FFFF,
    Green = 0x008000,
    Magenta = 0xFF00FF,
    Red = 0xFF0000,
    White = 0xFFFFFF,
    Yellow = 0xFFFF00,
  };

  constexpr CRGB() : r(0), g(0), b(0) {}
  constexpr CRGB(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
  constexpr CRGB(uint32_t rgb) : r((rgb >> 16) & 0xFF), g((rgb >> 8) & 0xFF), b(rgb & 0xFF) {}

  // FastLED nscale8 with FASTLED_SCALE8_FIXED: (v * (1 + scale)) >> 8
  CRGB &nscale8(uint8_t scale) {
    const uint16_t s = (uint16_t)scale + 1;
    r = (uint8_t)(((uint16_t)r * s) >> 8);
    g = (uint8_t)(((uint16_t)g * s) >> 8);
    b = (uint8_t)(((uint16_t)b * s) >> 8);
    return *this;
  }
};

static_assert(sizeof(CRGB) == 3, "CRGB must be 3 bytes: LedStrip::show takes packed bytes");

// Called from ~60 machine LED functions: keep one shared copy, do not inline
// the loop into each. Constructors are constexpr: the machines' static CRGB
// tables must stay constant-initialized (dynamic init cost 7 KB of flash).
__attribute__((noinline)) inline void fill_solid(CRGB *leds, int count, const CRGB &color) {
  for (int i = 0; i < count; i++) {
    leds[i] = color;
  }
}

// FastLED blend8 with FASTLED_SCALE8_FIXED: (a*256 + b + (b - a) * amount) >> 8
inline uint8_t _blend8(uint8_t a, uint8_t b, uint8_t amountOfB) {
  uint16_t partial = (uint16_t)((a << 8) | b);
  partial = (uint16_t)(partial + b * amountOfB);
  partial = (uint16_t)(partial - a * amountOfB);
  return (uint8_t)(partial >> 8);
}

inline CRGB &nblend(CRGB &existing, const CRGB &overlay, uint8_t amountOfOverlay) {
  if (amountOfOverlay == 0) {
    return existing;
  }
  if (amountOfOverlay == 255) {
    existing = overlay;
    return existing;
  }
  existing.r = _blend8(existing.r, overlay.r, amountOfOverlay);
  existing.g = _blend8(existing.g, overlay.g, amountOfOverlay);
  existing.b = _blend8(existing.b, overlay.b, amountOfOverlay);
  return existing;
}

} // namespace hal

using hal::CRGB;
using hal::fill_solid;
using hal::nblend;

#endif // HAL_LED_COLOR_H
