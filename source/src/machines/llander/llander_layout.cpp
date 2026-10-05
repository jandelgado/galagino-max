#include "llander_layout.h"

namespace llander_layout {

// native picture, 800 units high (MAME visarea 270..1070); y range fitted
// against a MAME snapshot (landing pad labels sit at y 24). Scaled 15/64
// onto 240 columns x 188 rows starting at panel row 50
static const vec2d::Rect VISIBLE = {0, -19, 1023, 781};
static const vec2d::Rect PANEL = {0, 0, 239, 287};
static const int16_t TOP_BORDER = 50;
static const int SCALE_MUL = 15, SCALE_SHIFT = 6;

// HUD text (anchor at or above HUD_Y) leaves the picture: the left block
// goes to the top border, the right block to the bottom border, scaled
// 2.25x (panel factor 15/64 * 9/4 = 135/256)
static const int16_t HUD_Y = 680, HUD_TOP = 760, HUD_SPLIT_X = 512;
static const int16_t HUD_LEFT_X = 100, HUD_RIGHT_X = 600, HUD_COL = 34;
static const int16_t HUD_LEFT_ROW = 2, HUD_RIGHT_ROW = 241;
static const int32_t HUD_MUL = 135, HUD_DIV = 256;

// other text 2x around the run's center, less if it would leave the
// screen (scale in 1/256)
static const int32_t MSG_SCALE = 512, ONE = 256;
static const int16_t NATIVE_MAX_X = 1023;

// The lander (body intensity 12, flame 15) and its crash wreck are the
// only plain lines with these intensities, drawn as one run. At panel
// scale the lander is 3 px wide; draw it 2x, growing up from its lowest
// body point so it still stands on the ground.
static const uint8_t LANDER = 12, FLAME = 15;
static const int32_t LANDER_SCALE = 512;

static bool is_lander(const vec2d::Line &l) {
  return l.color == LANDER || l.color == FLAME;
}

static void to_playfield(vec2d::Line &l) {
  l.x0 = (l.x0 * SCALE_MUL) >> SCALE_SHIFT;
  l.x1 = (l.x1 * SCALE_MUL) >> SCALE_SHIFT;
  l.y0 = TOP_BORDER + (((VISIBLE.y1 - l.y0) * SCALE_MUL) >> SCALE_SHIFT);
  l.y1 = TOP_BORDER + (((VISIBLE.y1 - l.y1) * SCALE_MUL) >> SCALE_SHIFT);
}

static void to_hud(vec2d::Line &l, int16_t ax) {
  const bool left = ax < HUD_SPLIT_X;
  const int16_t sx = left ? HUD_LEFT_X : HUD_RIGHT_X;
  const int16_t row = left ? HUD_LEFT_ROW : HUD_RIGHT_ROW;
  l.x0 = HUD_COL + (l.x0 - sx) * HUD_MUL / HUD_DIV;
  l.x1 = HUD_COL + (l.x1 - sx) * HUD_MUL / HUD_DIV;
  l.y0 = row + (HUD_TOP - l.y0) * HUD_MUL / HUD_DIV;
  l.y1 = row + (HUD_TOP - l.y1) * HUD_MUL / HUD_DIV;
}

// largest scale up to MSG_SCALE that keeps a run of half width `half`
// around cx inside the screen, at least 1
static int32_t run_scale(int16_t cx, int16_t half) {
  const int16_t room = cx < NATIVE_MAX_X - cx ? cx : NATIVE_MAX_X - cx;
  const int32_t fit = room > 0 ? (int32_t)room * ONE / half : ONE;
  return fit < MSG_SCALE ? (fit > ONE ? fit : ONE) : MSG_SCALE;
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
    if (is_lander(first)) {
      // pivot: bottom center of the body, flame excluded
      uint16_t end = i;
      int16_t xmin = INT16_MAX, xmax = INT16_MIN, ymin = INT16_MAX;
      for (; end < count && is_lander(lines[end]); end++) {
        const vec2d::Line &l = lines[end];
        if (l.color != LANDER) {
          continue;
        }
        xmin = l.x0 < xmin ? l.x0 : xmin;
        xmin = l.x1 < xmin ? l.x1 : xmin;
        xmax = l.x0 > xmax ? l.x0 : xmax;
        xmax = l.x1 > xmax ? l.x1 : xmax;
        ymin = l.y0 < ymin ? l.y0 : ymin;
        ymin = l.y1 < ymin ? l.y1 : ymin;
      }
      const bool body = ymin != INT16_MAX;
      const int16_t px = (xmin + xmax) / 2;

      for (; i < end; i++) {
        vec2d::Line l = lines[i];
        if (body) {
          scale_about(l, px, ymin, LANDER_SCALE);
        }
        if (!vec2d::clip(l, VISIBLE)) {
          continue;
        }
        to_playfield(l);
        lines[kept++] = l;
      }
      continue;
    }

    if (!(first.color & dvg::GLYPH)) {
      vec2d::Line l = first;
      i++;
      if (!vec2d::clip(l, VISIBLE)) {
        continue;
      }
      to_playfield(l);
      lines[kept++] = l;
      continue;
    }

    // one text run: consecutive glyph lines with the same anchor
    const int16_t ax = first.dx, ay = first.dy;
    uint16_t end = i;
    int16_t xmax = ax + 1;
    while (end < count && (lines[end].color & dvg::GLYPH) && lines[end].dx == ax &&
           lines[end].dy == ay) {
      xmax = lines[end].x0 > xmax ? lines[end].x0 : xmax;
      xmax = lines[end].x1 > xmax ? lines[end].x1 : xmax;
      end++;
    }
    const int16_t half = (xmax - ax + 1) / 2;
    const int16_t cx = ax + half;
    const int32_t s = run_scale(cx, half);

    for (; i < end; i++) {
      vec2d::Line l = lines[i];
      l.color &= ~dvg::GLYPH;
      if (ay >= HUD_Y) {
        to_hud(l, ax);
        if (!vec2d::clip(l, PANEL)) {
          continue;
        }
      } else {
        scale_about(l, cx, ay, s);
        if (!vec2d::clip(l, VISIBLE)) {
          continue;
        }
        to_playfield(l);
      }
      lines[kept++] = l;
    }
  }
  return kept;
}

} // namespace llander_layout
