#include "bzone_layout.h"

namespace bzone_layout {

static const vec2d::Rect VISIBLE = {32, 0, 543, 399};
static const int SCALE_MUL = 15, SCALE_SHIFT = 5;
static const int16_t TOP_BORDER = 51;

static_assert(TOP_BORDER + ((80 * SCALE_MUL) >> SCALE_SHIFT) == RED_STRIPS * 8,
              "overlay boundary on a strip edge");

// glyphs are 12 units high at binary scale 1, 6 at 2
static const int16_t SMALL_TEXT_H = 9;
// text scale in 1/256
static const int32_t TEXT_SCALE = 512, ONE = 256;

static void to_panel(vec2d::Line &l) {
  l.x0 = ((l.x0 - VISIBLE.x0) * SCALE_MUL) >> SCALE_SHIFT;
  l.x1 = ((l.x1 - VISIBLE.x0) * SCALE_MUL) >> SCALE_SHIFT;
  l.y0 = TOP_BORDER + ((l.y0 * SCALE_MUL) >> SCALE_SHIFT);
  l.y1 = TOP_BORDER + ((l.y1 * SCALE_MUL) >> SCALE_SHIFT);
}

// largest scale up to TEXT_SCALE that keeps a run of half width `half`
// around cx inside the visible area, at least 1
static int32_t run_scale(int16_t cx, int16_t half) {
  const int16_t left = cx - VISIBLE.x0, right = VISIBLE.x1 - cx;
  const int16_t room = left < right ? left : right;
  const int32_t fit = room > 0 ? (int32_t)room * ONE / half : ONE;
  return fit < TEXT_SCALE ? (fit > ONE ? fit : ONE) : TEXT_SCALE;
}

static void scale_about(vec2d::Line &l, int16_t cx, int16_t cy, int32_t s) {
  l.x0 = cx + (l.x0 - cx) * s / ONE;
  l.x1 = cx + (l.x1 - cx) * s / ONE;
  l.y0 = cy + (l.y0 - cy) * s / ONE;
  l.y1 = cy + (l.y1 - cy) * s / ONE;
}

uint16_t map(vec2d::Line *lines, uint16_t count) {
  // writes go to lines[kept] with kept <= i: unread lines stay intact
  uint16_t kept = 0;
  for (uint16_t i = 0; i < count;) {
    const vec2d::Line first = lines[i];
    if (!(first.color & avg::GLYPH)) {
      vec2d::Line l = first;
      i++;
      if (vec2d::clip(l, VISIBLE)) {
        to_panel(l);
        lines[kept++] = l;
      }
      continue;
    }

    // one text run: consecutive glyph lines with the same anchor (left
    // end of the baseline, glyphs grow up)
    const int16_t ax = first.dx, ay = first.dy;
    uint16_t end = i;
    int16_t xmax = ax + 1, ymin = ay;
    for (; end < count && (lines[end].color & avg::GLYPH) && lines[end].dx == ax &&
           lines[end].dy == ay;
         end++) {
      const vec2d::Line &l = lines[end];
      xmax = l.x0 > xmax ? l.x0 : xmax;
      xmax = l.x1 > xmax ? l.x1 : xmax;
      ymin = l.y0 < ymin ? l.y0 : ymin;
      ymin = l.y1 < ymin ? l.y1 : ymin;
    }
    const int16_t half = (xmax - ax + 1) / 2;
    const int16_t cx = ax + half;
    const int32_t s = ay - ymin <= SMALL_TEXT_H ? run_scale(cx, half) : ONE;

    for (; i < end; i++) {
      vec2d::Line l = lines[i];
      l.color &= ~avg::GLYPH;
      scale_about(l, cx, ay, s);
      if (vec2d::clip(l, VISIBLE)) {
        to_panel(l);
        lines[kept++] = l;
      }
    }
  }
  return kept;
}

} // namespace bzone_layout
