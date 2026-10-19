#include "bwidow_layout.h"
#include "../../emulation/avg.h"

namespace bwidow_layout {

static const int32_t SCALE_SHIFT = 10;

static void flip_line(const Screen &s, vec2d::Line &l, uint8_t flip) {
  if (flip & FLIP_X) {
    l.x0 = 2 * s.xcenter - l.x0;
    l.x1 = 2 * s.xcenter - l.x1;
  }
  if (flip & FLIP_Y) {
    l.y0 = 2 * s.ycenter - l.y0;
    l.y1 = 2 * s.ycenter - l.y1;
  }
}

static int16_t to_panel(const Screen &s, int16_t v) { return (v * s.scale_mul) >> SCALE_SHIFT; }

uint16_t map(const Screen &screen, vec2d::Line *lines, uint16_t count, uint8_t flip) {
  uint16_t kept = 0;
  for (uint16_t i = 0; i < count; i++) {
    vec2d::Line l = lines[i];
    flip_line(screen, l, flip);
    if (!vec2d::clip(l, screen.visible)) {
      continue;
    }

    l.x0 = to_panel(screen, l.x0);
    l.x1 = to_panel(screen, l.x1);
    l.y0 = screen.top_border + to_panel(screen, l.y0);
    l.y1 = screen.top_border + to_panel(screen, l.y1);
    lines[kept++] = l;
  }
  return kept;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

// MAME color111 times the level; levels ramp from a floor like
// dvgMachine so dim lines stay visible on the LCD
void palette(uint16_t *pal) {
  for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
    const uint8_t color = i >> avg::SW_COLOR_SHIFT, level = i & 0xf;
    const uint8_t l = level ? 80 + level * 11 : 0;
    pal[i] = rgb565((color & 4) ? l : 0, (color & 2) ? l : 0, (color & 1) ? l : 0);
  }
}

} // namespace bwidow_layout
