#ifndef HAL_RP2350_DVI_LINE_H
#define HAL_RP2350_DVI_LINE_H

// DVI output line builders: source rows (RGB565) to HSTX words, two pixels
// per word, the first pixel in the low half. No SDK dependencies, so host
// tests cover them. Row scaling follows pico-galagino's
// pico_shared/drivers/pico_hdmi/hstx_scale.h (GPLv3).

#include <stdint.h>

namespace hal {
namespace dvi {

// Mirror a row horizontally (flipped machines).
enum class Mirror : uint8_t { OFF, ON };

static const uint32_t OUT_LINES = 480;

struct Orientation {
  Mirror x;
  Mirror y;
};

// Machine flips as the SPI panel applies them: flipY turns the screen by
// 180 degrees (MADCTL MY|MX), flipX mirrors it horizontally on top.
static inline Orientation orientation(char flipY, char flipX) {
  const bool x = (flipY != 0) != (flipX != 0);
  return Orientation{x ? Mirror::ON : Mirror::OFF, flipY ? Mirror::ON : Mirror::OFF};
}

// SPI byte order (as machines render) to native RGB565.
static inline uint16_t swap565(uint16_t spi) {
  return (uint16_t)((spi << 8) | (spi >> 8));
}

static inline void copySwapped(const uint16_t *spi, uint16_t *out, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    out[i] = swap565(spi[i]);
  }
}

// Per-channel average, rounded down; a flat colour stays the same.
static inline uint16_t avg565(uint16_t a, uint16_t b) {
  return (uint16_t)((a & b) + (((a ^ b) & 0xF7DE) >> 1));
}

// a and b weighted (4 - q) : q, q in quarters 0..4.
static inline uint16_t blend565(uint16_t a, uint16_t b, uint32_t q) {
  if (q == 0) {
    return a;
  }
  if (q >= 4) {
    return b;
  }
  const uint16_t half = avg565(a, b);
  if (q == 2) {
    return half;
  }

  // 1/4 or 3/4: the average once more with the nearer row
  return avg565(q == 1 ? a : b, half);
}

// Output lines of a `rows` high picture in 2x mode: 2x when it fits the
// screen, else 1.5x (288 rows -> 432 lines).
static inline constexpr uint32_t scaledLines(uint32_t rows) {
  return rows * 2 <= OUT_LINES ? rows * 2 : rows * 3 / 2;
}

// The row output line `line` of `lines` shows. *q is the weight of the row
// below it in quarters: 0 shows the row alone, 1..3 blends it with the next
// row (which then exists). 288 rows on 432 lines: A, A/2 + B/2, B, ...
static inline uint32_t scaleRow(uint32_t line, uint32_t rows, uint32_t lines, uint32_t *q) {
  // positions in 1/lines of a row
  const uint32_t start = line * rows;
  uint32_t row = start / lines;
  const uint32_t end = start + rows;
  const uint32_t boundary = (row + 1) * lines;
  uint32_t w = end > boundary ? ((end - boundary) * 4 + rows / 2) / rows : 0;
  if (w == 4) {
    row++;
    w = 0;
  }
  *q = w;
  return row;
}

// 1x: `width` pixels (even) into width / 2 words.
static inline void line1x(const uint16_t *row, uint32_t *out, uint32_t width, Mirror mirror) {
  if (mirror == Mirror::ON) {
    for (uint32_t x = 0; x < width; x += 2) {
      *out++ = row[width - 1 - x] | (uint32_t)row[width - 2 - x] << 16;
    }
    return;
  }
  for (uint32_t x = 0; x < width; x += 2) {
    *out++ = row[x] | (uint32_t)row[x + 1] << 16;
  }
}

// 2x wide: every pixel doubled into one word; q > 0 blends row a with b.
static inline void line2x(const uint16_t *a, const uint16_t *b, uint32_t q, uint32_t *out,
                          uint32_t width, Mirror mirror) {
  const int32_t step = mirror == Mirror::ON ? -1 : 1;
  int32_t x = mirror == Mirror::ON ? (int32_t)width - 1 : 0;

  if (q == 0) {
    for (uint32_t i = 0; i < width; i++, x += step) {
      *out++ = a[x] * 0x10001u;
    }
    return;
  }
  for (uint32_t i = 0; i < width; i++, x += step) {
    *out++ = blend565(a[x], b[x], q) * 0x10001u;
  }
}

} // namespace dvi
} // namespace hal

#endif // HAL_RP2350_DVI_LINE_H
