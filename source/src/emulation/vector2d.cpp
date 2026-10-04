#include "vector2d.h"

namespace vec2d {

enum { LEFT = 1, RIGHT = 2, TOP = 4, BOTTOM = 8 };

static uint8_t outcode(int32_t x, int32_t y, const Rect &r) {
  return (x < r.x0 ? LEFT : x > r.x1 ? RIGHT : 0) |
         (y < r.y0 ? TOP : y > r.y1 ? BOTTOM : 0);
}

// n / d rounded to nearest, d != 0
static int32_t div_round(int32_t n, int32_t d) {
  if (d < 0) { n = -n; d = -d; }
  return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

bool clip(Line &l, const Rect &r) {
  int32_t x0 = l.x0, y0 = l.y0, x1 = l.x1, y1 = l.y1;
  uint8_t c0 = outcode(x0, y0, r), c1 = outcode(x1, y1, r);
  // each pass puts one endpoint on an edge; 4 passes suffice, the bound
  // only guards against rounding ping-pong
  for (uint8_t pass = 0;; pass++) {
    if (!(c0 | c1)) {
      break;
    }
    if (pass == 8 || (c0 & c1)) {
      return false;
    }
    // move the endpoint that is outside onto the violated edge
    const uint8_t c = c0 ? c0 : c1;
    int32_t x, y;
    if (c & TOP) {
      y = r.y0; x = x0 + div_round((x1 - x0) * (y - y0), y1 - y0);
    } else if (c & BOTTOM) {
      y = r.y1; x = x0 + div_round((x1 - x0) * (y - y0), y1 - y0);
    } else if (c & LEFT) {
      x = r.x0; y = y0 + div_round((y1 - y0) * (x - x0), x1 - x0);
    } else {
      x = r.x1; y = y0 + div_round((y1 - y0) * (x - x0), x1 - x0);
    }
    if (c == c0) { x0 = x; y0 = y; c0 = outcode(x0, y0, r); }
    else         { x1 = x; y1 = y; c1 = outcode(x1, y1, r); }
  }
  l.x0 = x0; l.y0 = y0; l.x1 = x1; l.y1 = y1;
  return true;
}

void StripRasterizer::init(uint16_t *next_, uint16_t *active_, uint16_t *head_, uint8_t strips_,
                           uint16_t *carry_) {
  next = next_; active = active_; head = head_; strips = strips_; carry = carry_;
}

void StripRasterizer::begin(Line *lines_, uint16_t count) {
  lines = lines_;
  active_count = 0;
  for (uint8_t s = 0; s < strips; s++) {
    head[s] = NONE;
  }
  for (uint16_t i = 0; i < count; i++) {
    Line &l = lines[i];
    if (l.y0 > l.y1) {
      int16_t t = l.x0; l.x0 = l.x1; l.x1 = t;
      t = l.y0; l.y0 = l.y1; l.y1 = t;
    }
    l.sx = l.x1 < l.x0;
    l.dx = l.sx ? l.x0 - l.x1 : l.x1 - l.x0;
    l.dy = l.y1 - l.y0;
    l.err = l.dx - l.dy;
    const uint8_t s = l.y0 / STRIP_H;
    next[i] = head[s];
    head[s] = i;
  }
}

static inline void halo(uint16_t *p, uint16_t g) {
  if (!*p) {
    *p = g;
  }
}

// Glow of the pixels line l has in row (the next strip's first) into dst
// (this strip's last row), stepping a copy of the Bresenham state.
VEC2D_IRAM void StripRasterizer::glow_above(const Line &l, int16_t x, int16_t y, int16_t err,
                                            int16_t row, uint16_t *dst, uint16_t g) {
  const int16_t step = l.sx ? -1 : 1;
  while (y == row) {
    halo(dst + x, g);
    if (x == l.x1 && y == l.y1) {
      break;
    }
    const int32_t e2 = 2 * (int32_t)err;
    if (e2 > -l.dy) { err -= l.dy; x += step; }
    if (e2 < l.dx) { err += l.dx; y++; }
  }
}

VEC2D_IRAM void StripRasterizer::render(uint8_t strip, uint16_t *buf, int16_t width,
                                        const uint16_t *palette, const uint16_t *glow) {
  for (uint16_t i = head[strip]; i != NONE; i = next[i]) {
    active[active_count++] = i;
  }

  // glow below the previous strip's last row
  if (glow) {
    for (int16_t x = 0; x < width; x++) {
      if (strip) {
        buf[x] = carry[x];
      }
      carry[x] = 0;
    }
  }

  const int16_t row0 = strip * STRIP_H, end = row0 + STRIP_H;
  uint16_t *const last = buf + (STRIP_H - 1) * width;
  for (uint16_t a = 0; a < active_count;) {
    Line &l = lines[active[a]];
    const uint16_t c = palette[l.color];
    const uint16_t g = glow ? glow[l.color] : 0;
    const int16_t step = l.sx ? -1 : 1;
    int16_t x = l.x0, y = l.y0, err = l.err;
    bool done = false;
    while (y < end) {
      uint16_t *p = buf + (y - row0) * width + x;
      *p = c;
      if (g) {
        if (x > 0) halo(p - 1, g);
        if (x < width - 1) halo(p + 1, g);
        if (y > row0) halo(p - width, g);
        if (y < end - 1) halo(p + width, g);
        else halo(carry + x, g);
      }
      if (x == l.x1 && y == l.y1) { done = true; break; }
      const int32_t e2 = 2 * (int32_t)err;
      if (e2 > -l.dy) { err -= l.dy; x += step; }
      if (e2 < l.dx) { err += l.dx; y++; }
    }
    if (done) {
      active[a] = active[--active_count]; // swap-remove, revisit slot a
    } else {
      if (g) {
        glow_above(l, x, y, err, end, last, g);
      }
      l.x0 = x; l.y0 = y; l.err = err;
      a++;
    }
  }

  // glow above lines starting in the next strip's first row
  if (glow && strip + 1 < strips) {
    for (uint16_t i = head[strip + 1]; i != NONE; i = next[i]) {
      const Line &l = lines[i];
      const uint16_t g = glow[l.color];
      if (g && l.y0 == end) {
        glow_above(l, l.x0, l.y0, l.err, end, last, g);
      }
    }
  }
}

} // namespace vec2d
