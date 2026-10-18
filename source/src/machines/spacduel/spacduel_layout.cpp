#include "spacduel_layout.h"
#include "../../emulation/avg.h"

namespace spacduel_layout {

static const vec2d::Rect VISIBLE = {0, 0, 540, 400};
static const int16_t XCENTER = 270, YCENTER = 200; // MAME avg_device::device_start
static const int32_t SCALE_MUL = 454, SCALE_SHIFT = 10; // 541 -> 240 columns
static const int16_t TOP_BORDER = 55;                   // (288 - 178) / 2

static void flip_line(vec2d::Line &l, uint8_t flip) {
  if (flip & FLIP_X) {
    l.x0 = 2 * XCENTER - l.x0;
    l.x1 = 2 * XCENTER - l.x1;
  }
  if (flip & FLIP_Y) {
    l.y0 = 2 * YCENTER - l.y0;
    l.y1 = 2 * YCENTER - l.y1;
  }
}

static int16_t to_panel(int16_t v) { return (v * SCALE_MUL) >> SCALE_SHIFT; }

uint16_t map(vec2d::Line *lines, uint16_t count, uint8_t flip) {
  uint16_t kept = 0;
  for (uint16_t i = 0; i < count; i++) {
    vec2d::Line l = lines[i];
    flip_line(l, flip);
    if (!vec2d::clip(l, VISIBLE)) {
      continue;
    }

    l.x0 = to_panel(l.x0);
    l.x1 = to_panel(l.x1);
    l.y0 = TOP_BORDER + to_panel(l.y0);
    l.y1 = TOP_BORDER + to_panel(l.y1);
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

} // namespace spacduel_layout
