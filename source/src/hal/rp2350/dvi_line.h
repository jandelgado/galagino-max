#ifndef HAL_RP2350_DVI_LINE_H
#define HAL_RP2350_DVI_LINE_H

// DVI output line builders: source rows (RGB565 in SPI byte order, as
// machines render) to HSTX words (native RGB565), two pixels per word, the
// first pixel in the low half. Rows have an even width and are read a pixel
// pair (one 32-bit word) at a time: the builders run 432 times a frame in
// the scanout IRQ. No SDK dependencies, so host tests cover them. Row scaling follows pico-galagino's
// pico_shared/drivers/pico_hdmi/hstx_scale.h (GPLv3).

#include <stdint.h>
#include <string.h>

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

// Both pixels of a pair from SPI byte order to native (one REV16 on ARM).
static inline uint32_t swapPair(uint32_t spi) {
  return ((spi & 0x00ff00ffu) << 8) | ((spi >> 8) & 0x00ff00ffu);
}

// Pixels x and x + 1 of an SPI order row, native, pixel x in the low half.
static inline uint32_t pairAt(const uint16_t *row, uint32_t x) {
  uint32_t pair;
  memcpy(&pair, row + x, sizeof(pair));
  return swapPair(pair);
}

// Per-channel average, rounded down; a flat colour stays the same.
static inline uint16_t avg565(uint16_t a, uint16_t b) {
  return (uint16_t)((a & b) + (((a ^ b) & 0xF7DE) >> 1));
}

// avg565 of both pixels of two pairs at once.
static inline uint32_t avgPair(uint32_t a, uint32_t b) {
  return (a & b) + (((a ^ b) & 0xF7DEF7DEu) >> 1);
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

// Framebuffer row of line `line` (0..7) of the strip-th strip in scan order:
// flipY strips arrive bottom strip first (stripIndex), their lines reversed.
static inline uint32_t frameRow(uint32_t strip, uint32_t line, Mirror flipY) {
  return strip * 8 + (flipY == Mirror::ON ? 7 - line : line);
}

// Scanout watchdog: more than maxFps frames per second over `us`
// microseconds means the DMA chain lost its framing.
static inline bool overRate(uint32_t frames, uint32_t us, uint32_t maxFps) {
  return (uint64_t)frames * 1000000u > (uint64_t)maxFps * us;
}

// Base of the pass a beam position is in (beam = pass * span + rows read).
static inline uint32_t passBase(uint32_t beam, uint32_t span) {
  return beam & ~(span - 1);
}

// 1x: `width` pixels into width / 2 words.
static inline void line1x(const uint16_t *row, uint32_t *out, uint32_t width, Mirror mirror) {
  if (mirror == Mirror::ON) {
    for (uint32_t x = 0; x < width; x += 2) {
      const uint32_t pair = pairAt(row, width - 2 - x);
      *out++ = pair >> 16 | pair << 16;
    }
    return;
  }
  for (uint32_t x = 0; x < width; x += 2) {
    *out++ = pairAt(row, x);
  }
}

// How line2x mixes row b into row a: not, half and half (q = 2, all the
// blended lines of 288 rows on 432), or by quarters (blend565).
enum class Mix : uint8_t { NONE, HALF, QUARTERS };

template <Mirror MIRROR, Mix MIX>
static inline void line2xPairs(const uint16_t *a, const uint16_t *b, uint32_t q, uint32_t *out,
                               uint32_t width) {
  for (uint32_t i = 0; i < width; i += 2, out += 2) {
    const uint32_t x = MIRROR == Mirror::ON ? width - 2 - i : i;
    uint32_t pair = pairAt(a, x);
    if (MIX == Mix::HALF) {
      pair = avgPair(pair, pairAt(b, x));
    }
    if (MIX == Mix::QUARTERS) {
      const uint32_t other = pairAt(b, x);
      pair = blend565((uint16_t)pair, (uint16_t)other, q) |
             (uint32_t)blend565((uint16_t)(pair >> 16), (uint16_t)(other >> 16), q) << 16;
    }

    const uint32_t lo = pair & 0xffffu;
    const uint32_t hi = pair >> 16;
    const uint32_t first = MIRROR == Mirror::ON ? hi : lo;
    const uint32_t second = MIRROR == Mirror::ON ? lo : hi;
    out[0] = first | first << 16;
    out[1] = second | second << 16;
  }
}

// 2x wide: every pixel doubled into one word; q > 0 blends row a with b.
static inline void line2x(const uint16_t *a, const uint16_t *b, uint32_t q, uint32_t *out,
                          uint32_t width, Mirror mirror) {
  const Mix mix = q == 0 ? Mix::NONE : q == 2 ? Mix::HALF : Mix::QUARTERS;
  if (mirror == Mirror::ON) {
    if (mix == Mix::NONE) {
      line2xPairs<Mirror::ON, Mix::NONE>(a, b, q, out, width);
    } else if (mix == Mix::HALF) {
      line2xPairs<Mirror::ON, Mix::HALF>(a, b, q, out, width);
    } else {
      line2xPairs<Mirror::ON, Mix::QUARTERS>(a, b, q, out, width);
    }
    return;
  }
  if (mix == Mix::NONE) {
    line2xPairs<Mirror::OFF, Mix::NONE>(a, b, q, out, width);
  } else if (mix == Mix::HALF) {
    line2xPairs<Mirror::OFF, Mix::HALF>(a, b, q, out, width);
  } else {
    line2xPairs<Mirror::OFF, Mix::QUARTERS>(a, b, q, out, width);
  }
}

} // namespace dvi
} // namespace hal

#endif // HAL_RP2350_DVI_LINE_H
