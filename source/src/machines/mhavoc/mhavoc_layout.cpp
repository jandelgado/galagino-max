#include "mhavoc_layout.h"
#include "../../emulation/avg.h"

namespace mhavoc_layout {

namespace {

// one line of text: glyph runs [start, end) and their box
struct Text {
  uint16_t start, end;
  int16_t x0, y0, x1, y1;
  int8_t group; // index of the merged line it belongs to
};

const uint8_t MAX_TEXTS = 64;            // more runs per list stay as they are
const int16_t MERGE_GAP = 12;            // runs closer on one row are one line
const int16_t MIN_GAP = 1;               // kept between grown neighbors
const int16_t F16 = 16 * TEXT_NUM / TEXT_DEN; // factor in 1/16

bool overlap(int16_t a0, int16_t a1, int16_t b0, int16_t b1) { return a0 <= b1 && b0 <= a1; }

// factor (1/16) that grows size by at most 2 * room, capped at F16
int16_t factor(int16_t size, int16_t room) {
  if (size <= 0 || room <= 0) {
    return 16;
  }
  const int32_t f = 16 * int32_t(size + 2 * room) / size;
  return f < F16 ? int16_t(f) : F16;
}

int16_t scale(int16_t v, int16_t center, int16_t f16) { return center + (v - center) * f16 / 16; }

} // namespace

void enlarge_text(vec2d::Line *lines, uint16_t count) {
  const vec2d::Rect &vis = SCREEN.visible;
  Text t[MAX_TEXTS];
  uint8_t n = 0;

  // runs: consecutive glyph lines with one anchor
  for (uint16_t i = 0; i < count && n < MAX_TEXTS;) {
    if (!(lines[i].color & avg::GLYPH)) {
      i++;
      continue;
    }
    const int16_t ax = lines[i].dx, ay = lines[i].dy;
    Text &r = t[n];
    r = {i, i, INT16_MAX, INT16_MAX, INT16_MIN, INT16_MIN, int8_t(n)};
    for (; r.end < count && (lines[r.end].color & avg::GLYPH) && lines[r.end].dx == ax &&
           lines[r.end].dy == ay;
         r.end++) {
      const vec2d::Line &l = lines[r.end];
      r.x0 = l.x0 < r.x0 ? l.x0 : r.x0, r.x0 = l.x1 < r.x0 ? l.x1 : r.x0;
      r.x1 = l.x0 > r.x1 ? l.x0 : r.x1, r.x1 = l.x1 > r.x1 ? l.x1 : r.x1;
      r.y0 = l.y0 < r.y0 ? l.y0 : r.y0, r.y0 = l.y1 < r.y0 ? l.y1 : r.y0;
      r.y1 = l.y0 > r.y1 ? l.y0 : r.y1, r.y1 = l.y1 > r.y1 ? l.y1 : r.y1;
    }
    i = r.end;
    n++;
  }

  // merge runs side by side on one row into lines; group boxes in the
  // group leader (lowest index)
  for (uint8_t i = 0; i < n; i++) {
    for (uint8_t j = 0; j < i; j++) {
      Text &a = t[t[j].group], &b = t[i];
      if (b.group == i && overlap(a.y0, a.y1, b.y0, b.y1) &&
          overlap(a.x0 - MERGE_GAP, a.x1 + MERGE_GAP, b.x0, b.x1)) {
        b.group = t[j].group;
        a.x0 = b.x0 < a.x0 ? b.x0 : a.x0, a.x1 = b.x1 > a.x1 ? b.x1 : a.x1;
        a.y0 = b.y0 < a.y0 ? b.y0 : a.y0, a.y1 = b.y1 > a.y1 ? b.y1 : a.y1;
      }
    }
  }

  for (uint8_t g = 0; g < n; g++) {
    if (t[g].group != g) {
      continue;
    }
    const Text &a = t[g];
    int16_t fx = 16, fy = 16;
    if (a.y1 - a.y0 <= SMALL_TEXT) {
      // room per side: to the visarea edge, half the gap to other lines
      int16_t rx = a.x0 - vis.x0 < vis.x1 - a.x1 ? a.x0 - vis.x0 : vis.x1 - a.x1;
      int16_t ry = a.y0 - vis.y0 < vis.y1 - a.y1 ? a.y0 - vis.y0 : vis.y1 - a.y1;
      for (uint8_t h = 0; h < n; h++) {
        const Text &b = t[h];
        if (h == g || b.group != h) {
          continue;
        }
        if (overlap(a.y0, a.y1, b.y0, b.y1)) {
          const int16_t gap = (b.x0 > a.x1 ? b.x0 - a.x1 : a.x0 - b.x1) - MIN_GAP;
          rx = gap / 2 < rx ? gap / 2 : rx;
        } else if (overlap(a.x0, a.x1, b.x0, b.x1)) {
          const int16_t gap = (b.y0 > a.y1 ? b.y0 - a.y1 : a.y0 - b.y1) - MIN_GAP;
          ry = gap / 2 < ry ? gap / 2 : ry;
        }
      }
      fx = factor(a.x1 - a.x0, rx);
      fy = factor(a.y1 - a.y0, ry);
    }
    const int16_t cx = (a.x0 + a.x1) / 2, cy = (a.y0 + a.y1) / 2;
    for (uint8_t r = g; r < n; r++) {
      if (t[r].group != g) {
        continue;
      }
      for (uint16_t i = t[r].start; i < t[r].end; i++) {
        vec2d::Line &l = lines[i];
        l.color &= ~avg::GLYPH;
        l.x0 = scale(l.x0, cx, fx);
        l.x1 = scale(l.x1, cx, fx);
        l.y0 = scale(l.y0, cy, fy);
        l.y1 = scale(l.y1, cy, fy);
      }
    }
  }
  // runs beyond MAX_TEXTS keep their size
  for (uint16_t i = n ? t[n - 1].end : 0; i < count; i++) {
    lines[i].color &= ~avg::GLYPH;
  }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

// MAME mhavoc_strobe3: inverted color RAM bits, red = bit 3 * 0xcb + bit 2
// * 0x34, green bit 1, blue bit 0 (0xcb each). Levels 1..7 ramp from a
// floor like tempest_layout so dim lines stay visible on the LCD.
void palette(uint16_t *pal) {
  static const uint8_t LEVELS = avg::TP_LEVEL_MASK + 1;
  static_assert(PALETTE_SIZE == 16 * LEVELS, "palette covers every color RAM value and level");
  for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
    const uint8_t value = ~(i >> avg::TP_COLOR_SHIFT), level = i & avg::TP_LEVEL_MASK;
    const uint16_t ramp = level ? 80 + level * 25 : 0;
    const uint16_t gain = ramp < 255 ? ramp : 255;

    const uint16_t r = ((value & 8) ? 0xcb : 0) + ((value & 4) ? 0x34 : 0);
    const uint16_t g = (value & 2) ? 0xcb : 0;
    const uint16_t b = (value & 1) ? 0xcb : 0;
    pal[i] = rgb565(r * gain / 255, g * gain / 255, b * gain / 255);
  }
}

} // namespace mhavoc_layout
