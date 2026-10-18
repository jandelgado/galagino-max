#include "tempest_layout.h"
#include "../../emulation/avg.h"

namespace tempest_layout {

// beam area of MAME visarea (0, 580, 0, 570) after the swap, and center
static const vec2d::Rect VISIBLE = {5, 0, 575, 575};
static const int16_t XCENTER = 290, YCENTER = 285;

// 571 -> 240 columns, 581 -> 288 rows, as mul >> 10
static const int32_t COL_MUL = 430, ROW_MUL = 507, SCALE_SHIFT = 10;

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

static int16_t col(int16_t x) { return ((x - VISIBLE.x0) * COL_MUL) >> SCALE_SHIFT; }
static int16_t row(int16_t y) { return ((VISIBLE.y1 - y) * ROW_MUL) >> SCALE_SHIFT; }

uint16_t map(vec2d::Line *lines, uint16_t count, uint8_t flip) {
  uint16_t kept = 0;
  for (uint16_t i = 0; i < count; i++) {
    vec2d::Line l = lines[i];
    flip_line(l, flip);
    if (!vec2d::clip(l, VISIBLE)) {
      continue;
    }

    l.x0 = col(l.x0);
    l.x1 = col(l.x1);
    l.y0 = row(l.y0);
    l.y1 = row(l.y1);
    lines[kept++] = l;
  }
  return kept;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

// MAME tempest_strobe3: inverted color RAM bits, 0xf3 per gun, red bit 0
// adds 0x0c. Levels 1..7 ramp from a floor so dim lines stay visible on
// the LCD.
void palette(uint16_t *pal) {
  static const uint8_t LEVELS = avg::TP_LEVEL_MASK + 1;
  for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
    const uint8_t value = i >> avg::TP_COLOR_SHIFT, level = i & avg::TP_LEVEL_MASK;
    const uint16_t ramp = level ? 80 + level * 25 : 0;
    const uint16_t gain = ramp < 255 ? ramp : 255;

    const uint8_t r = ((value & 2) ? 0 : 0xf3) + ((value & 1) ? 0 : 0x0c);
    const uint8_t g = (value & 8) ? 0 : 0xf3;
    const uint8_t b = (value & 4) ? 0 : 0xf3;
    pal[i] = rgb565(r * gain / 255, g * gain / 255, b * gain / 255);
  }
  static_assert(PALETTE_SIZE == 16 * LEVELS, "palette covers every color RAM value and level");
}

} // namespace tempest_layout
