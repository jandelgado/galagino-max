#ifndef VECTOR2D_H
#define VECTOR2D_H

// 2D line drawing for vector machines without a framebuffer: lines are
// clipped once per frame, then rasterized strip by strip (galagino renders
// 8 rows at a time) with a Bresenham stepper whose state carries over from
// one strip to the next.

#include <stdint.h>

#if defined(ESP32) || defined(ESP_PLATFORM)
#  include "esp_attr.h"
#  define VEC2D_IRAM IRAM_ATTR
#else
#  define VEC2D_IRAM
#endif

namespace vec2d {

struct Rect { int16_t x0, y0, x1, y1; }; // inclusive

// x = column, y = row. After StripRasterizer::begin() x0/y0 hold the current
// Bresenham position and dx/dy/err/sx its state.
struct Line {
  int16_t x0, y0, x1, y1;
  int16_t dx, dy, err;
  uint8_t sx;    // 0: x steps +1, 1: x steps -1
  uint8_t color; // palette index
};
static_assert(sizeof(Line) == 16, "Line size is part of the Arena budget");

// Clips the segment to r (Cohen-Sutherland, intersections rounded to
// nearest). Returns false if nothing is left.
bool clip(Line &l, const Rect &r);

class StripRasterizer {
public:
  static const int STRIP_H = 8;
  static const uint16_t NONE = 0xffff;

  // next/active: one entry per line, head: one per strip, carry: one per
  // column, only needed for glow
  void init(uint16_t *next, uint16_t *active, uint16_t *head, uint8_t strips,
            uint16_t *carry = nullptr);
  // lines must lie inside [0, width) x [0, strips * 8)
  void begin(Line *lines, uint16_t count);
  // Plots all pixels with row in [strip * 8, strip * 8 + 8) into
  // buf[(row - strip * 8) * width + col]. Strips must follow in increasing
  // order after begin(), starting with strip 0. The caller clears buf.
  // glow (optional, needs carry): color per palette index for the 4
  // neighbors of each line pixel, drawn where no line is.
  void render(uint8_t strip, uint16_t *buf, int16_t width, const uint16_t *palette,
              const uint16_t *glow = nullptr);

private:
  void glow_above(const Line &l, int16_t x, int16_t y, int16_t err, int16_t row,
                  uint16_t *dst, uint16_t g);

  Line *lines = nullptr;
  uint16_t *next = nullptr, *active = nullptr, *head = nullptr, *carry = nullptr;
  uint16_t active_count = 0;
  uint8_t strips = 0;
};

} // namespace vec2d

#endif
