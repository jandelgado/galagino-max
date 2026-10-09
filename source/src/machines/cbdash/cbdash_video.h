#ifndef CBDASH_VIDEO_H
#define CBDASH_VIDEO_H

#include <stdint.h>

// Pure video helpers for cbdash (host tested), MAME decocass_v.cpp for the
// path cbdash uses: fg chars + 8 sprites, both decoded from charram the game
// writes at runtime. Background, center line, missiles and the 1bpp object
// stay off (mode_set 0x40, color_center_bot 0, missiles off screen).
//
// ROT270, native 256 x 240 (visible y 8..247) to 240 x 256 in 288 rows:
//   out column = native y - 8, out line = 271 - native x
// so strip r (8 lines) is exactly native tile column 33 - r, and line k of
// the strip is bit k of a char plane byte.
namespace cbdash_video {

constexpr int OUT_WIDTH = 240;
constexpr int FIRST_STRIP = 2;
constexpr int LAST_STRIP = 33;     // strip of native tile column 0
constexpr int VIS_Y0 = 8;          // first visible native line
constexpr int VIS_Y1 = 248;
constexpr int FIRST_TILE_ROW = VIS_Y0 / 8;
constexpr int LAST_TILE_ROW = VIS_Y1 / 8 - 1;

constexpr uint16_t FG_SIZE = 0x400;
constexpr uint16_t PLANE_SIZE = 0x2000;   // 3 planes, LSB first in charram
constexpr uint16_t CHARRAM_SIZE = 3 * PLANE_SIZE;
constexpr int PENS = 16;                  // 2 colors x 8 pens
constexpr int BG_PEN = 8;

// sprite list interleaved in fg RAM: slot i at (7 - i) * 0x80
constexpr int SPRITES = 8;
constexpr uint16_t SPRITE_STRIDE = 0x80;
constexpr uint16_t SPRITE_CODE = 0x20;
constexpr uint16_t SPRITE_Y = 0x40;
constexpr uint16_t SPRITE_X = 0x60;
constexpr int SPRITE_ORIGIN = 240;   // native position = 240 - register

// Playfield: native tile cols 2..25 (strips 8..31) x tile rows 1..30, a
// 15 x 12 cell window into the cave, 1 cell = 2 x 2 chars. HUD above it.
constexpr int PF_COL0 = 2;
constexpr int PF_COLS = 24;
constexpr int PF_FIRST_STRIP = LAST_STRIP - (PF_COL0 + PF_COLS - 1);
constexpr int PF_LAST_STRIP = LAST_STRIP - PF_COL0;
constexpr int PF_ROW0 = 1;
constexpr int PF_ROW1 = 30;
constexpr int CELL = 16;            // px, the game's scroll step
constexpr uint8_t TICK_FRAMES = 8;  // paced game tick, 16 px glide at 2 px per frame
constexpr uint8_t DEFAULT_PERIOD = TICK_FRAMES;
constexpr uint8_t MAX_PERIOD = 32;  // longer draw gaps = cave change, no glide

// what render reads from the emulated machine, latched at vblank
struct VideoState {
  uint8_t fg[FG_SIZE];     // c000: char codes + sprite list
  uint8_t color[FG_SIZE];  // c400: b0-1 = char code b8-9
  uint8_t palette[32];     // e000
  uint8_t ccb;             // e410: b0 char color, b1 sprite color
  uint8_t cam_x, cam_y;    // camera (cells) the playfield was drawn for
  uint8_t draws;           // playfield redraw counter
  uint8_t frame;           // emulated frame counter, also on skipped publishes
  uint8_t draw_frame;      // frame the last redraw was published in
};

// Smooth scroll: the game redraws the playfield once per tick and jumps
// one cell per step. Render glides from the previous view to the new one
// over one measured tick; the band that left the window comes from old.
//
//   tick:     cam 9 ------------- cam 10 ------------- cam 11
//   display:  9 -- glide 16 px --> 10 -- glide 16 px --> 11
//
// cam_x moves the window along native y (output columns), cam_y along
// native x (strips, opposite direction).
struct Scroll {
  uint8_t cam_x, cam_y;
  int8_t step_x, step_y;   // cells of the last step, old view = cam - step
  uint8_t draws;
  uint8_t age;             // frames since the step
  uint8_t period;          // frames between draws
  uint8_t since_draw;
  uint8_t frame;           // VideoState::frame seen last
};

// window offset this frame: output column y shows window y + ox, strip
// line at native x shows window x + oy
struct ScrollView {
  int8_t ox, oy;
  int8_t step_x, step_y;
};

inline uint8_t addSat(uint8_t a, uint8_t b) {
  return a + b < 0xff ? a + b : 0xff;
}

// per prepare_frame; have_old: old holds the view before v's camera.
// Time counts emulated frames: at VIDEO_HALF_RATE a tick of 11 frames is
// 5.5 display frames, display frame counts would jitter the period 5/6.
inline void scrollUpdate(Scroll &s, const VideoState &v, bool have_old) {
  const uint8_t elapsed = v.frame - s.frame;
  s.frame = v.frame;
  s.since_draw = addSat(s.since_draw, elapsed);
  s.age = addSat(s.age, elapsed);
  if (v.draws == s.draws) {
    return;
  }

  // the draw may be up to a display frame old, keep its phase
  const uint8_t lag = v.frame - v.draw_frame;
  const uint8_t since = s.since_draw - lag;
  const bool steady = s.since_draw >= lag && since <= MAX_PERIOD;
  if (steady) {
    s.period = since;
  }
  s.since_draw = lag;
  s.draws = v.draws;

  const int dx = v.cam_x - s.cam_x, dy = v.cam_y - s.cam_y;
  s.cam_x = v.cam_x;
  s.cam_y = v.cam_y;
  if (!dx && !dy) {
    return;
  }

  // jumps (new cave, first draw) snap
  const bool glide = have_old && steady && dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
  s.step_x = glide ? dx : 0;
  s.step_y = glide ? dy : 0;
  s.age = lag;
}

inline ScrollView scrollView(const Scroll &s) {
  const int rem = s.age < s.period ? CELL * (s.period - s.age) / s.period : 0;
  return {(int8_t)(-s.step_x * rem), (int8_t)(s.step_y * rem), s.step_x, s.step_y};
}

struct Sprite {
  int16_t x, y;            // native, y may be negative (wraps)
  uint8_t code;
  uint8_t flip_x, flip_y;
};

// palette byte, inverted: b0-2 red, b3-5 green, b6-7 blue (MAME pal3bit/pal2bit)
inline uint16_t color565(uint8_t data) {
  uint8_t d = (uint8_t)~data;
  uint8_t r = d & 7, g = (d >> 3) & 7, b = d >> 6;
  uint8_t r8 = (r << 5) | (r << 2) | (r >> 1);
  uint8_t g8 = (g << 5) | (g << 2) | (g >> 1);
  uint8_t b8 = (b << 6) | (b << 4) | (b << 2) | b;
  uint16_t rgb = ((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3);
  return (uint16_t)((rgb >> 8) | (rgb << 8)); // display byte order
}

// fg RAM is stored column by column, right to left
inline uint16_t fgOffset(uint8_t tile_col, uint8_t tile_row) {
  return (31 - tile_col) * 32 + tile_row;
}

inline uint8_t collectSprites(const VideoState &v, Sprite out[SPRITES]) {
  uint8_t n = 0;
  for (int i = 0; i < SPRITES; i++) {
    const uint8_t *s = v.fg + (SPRITES - 1 - i) * SPRITE_STRIDE;
    if (!(s[0] & 0x01)) {
      continue;
    }

    Sprite &o = out[n++];
    o.x = SPRITE_ORIGIN - s[SPRITE_X];
    o.y = SPRITE_ORIGIN - s[SPRITE_Y];
    o.code = s[SPRITE_CODE];
    o.flip_x = (s[0] & 0x04) != 0;
    o.flip_y = (s[0] & 0x02) != 0;
  }
  return n;
}

inline uint8_t planePen(const uint8_t *p, int bit) {
  return ((p[0] >> bit) & 1) | (((p[PLANE_SIZE] >> bit) & 1) << 1) | (((p[2 * PLANE_SIZE] >> bit) & 1) << 2);
}

inline void drawSprite(uint16_t *fb, int x0, const uint8_t *charram, const uint16_t *pal, const Sprite &s) {
  int from = s.x > x0 ? s.x : x0;
  int to = s.x + 15 < x0 + 7 ? s.x + 15 : x0 + 7;

  for (int x = from; x <= to; x++) {
    int sx = s.flip_x ? 15 - (x - s.x) : x - s.x;
    uint16_t *line = fb + (x0 + 7 - x) * OUT_WIDTH;

    for (int y = 0; y < 16; y++) {
      int ny = (s.y + y) & 0xff; // drawn at y and y + 256
      if (ny < VIS_Y0 || ny >= VIS_Y1) {
        continue;
      }
      int sy = s.flip_y ? 15 - y : y;
      const uint8_t *p = charram + s.code * 32 + (sx < 8 ? 16 + sy : sy);
      uint8_t pen = planePen(p, 7 - (sx & 7));
      if (pen) {
        line[ny - VIS_Y0] = pal[pen];
      }
    }
  }
}

// one 8 line strip = one native tile column: chars over pen 8, then sprites
inline void renderStrip(uint16_t *fb, int row, const uint8_t *charram, const VideoState &v,
                        const uint16_t *pal, const Sprite *spr, uint8_t count) {
  if (row < FIRST_STRIP || row > LAST_STRIP) {
    return;
  }

  const uint8_t tile_col = LAST_STRIP - row;
  const uint8_t *codes = v.fg + fgOffset(tile_col, 0);
  const uint8_t *banks = v.color + fgOffset(tile_col, 0);
  const uint16_t *cpal = pal + (v.ccb & 1) * 8;
  const uint16_t bg = pal[BG_PEN];

  for (int tr = FIRST_TILE_ROW; tr <= LAST_TILE_ROW; tr++) {
    uint16_t code = codes[tr] | ((banks[tr] & 3) << 8);
    const uint8_t *p = charram + code * 8;
    uint16_t *out = fb + tr * 8 - VIS_Y0;

    for (int y = 0; y < 8; y++) {
      uint8_t b0 = p[y], b1 = p[PLANE_SIZE + y], b2 = p[2 * PLANE_SIZE + y];
      for (int k = 0; k < 8; k++) {
        uint8_t pen = ((b0 >> k) & 1) | (((b1 >> k) & 1) << 1) | (((b2 >> k) & 1) << 2);
        out[k * OUT_WIDTH + y] = pen ? cpal[pen] : bg;
      }
    }
  }

  const uint16_t *spal = pal + ((v.ccb >> 1) & 1) * 8;
  const int x0 = 8 * tile_col;
  for (uint8_t i = 0; i < count; i++) {
    if (spr[i].x + 15 < x0 || spr[i].x > x0 + 7) {
      continue;
    }
    drawSprite(fb, x0, charram, spal, spr[i]);
  }
}

// char pixels of window tile (col, row): from the current view, else from
// the old one (scrolled out band), else nullptr (background)
inline bool inWindow(int col, int row) {
  return col >= PF_COL0 && col < PF_COL0 + PF_COLS && row >= PF_ROW0 && row <= PF_ROW1;
}

inline const uint8_t *windowTile(const uint8_t *charram, const VideoState &v, const VideoState &old,
                                 const ScrollView &s, int col, int row) {
  const VideoState *src = &v;
  if (!inWindow(col, row)) {
    src = &old;
    col -= 2 * s.step_y;
    row += 2 * s.step_x;
    if (!inWindow(col, row)) {
      return nullptr;
    }
  }

  const uint16_t offs = fgOffset(col, row);
  const uint16_t code = src->fg[offs] | ((src->color[offs] & 3) << 8);
  return charram + code * 8;
}

// floor(x / 8) for the small negative x a scroll offset produces
inline int tileOf(int x) {
  return ((x + 8 * CELL) >> 3) - CELL;
}

// strip lines k0..k1-1, all in window tile column col, line k0 at plane bit bit0
inline void shiftedLines(uint16_t *fb, int k0, int k1, int bit0, int col, const uint8_t *charram,
                         const VideoState &v, const VideoState &old, const ScrollView &s,
                         const uint16_t *cpal, uint16_t bg) {
  const int row0 = tileOf(VIS_Y0 + s.ox);
  const int row1 = tileOf(VIS_Y1 - 1 + s.ox);

  for (int tr = row0; tr <= row1; tr++) {
    const uint8_t *p = windowTile(charram, v, old, s, col, tr);
    const int out0 = tr * 8 - VIS_Y0 - s.ox;

    for (int y = 0; y < 8; y++) {
      const int out = out0 + y;
      if (out < 0 || out >= OUT_WIDTH) {
        continue;
      }

      if (!p) {
        for (int k = k0; k < k1; k++) {
          fb[k * OUT_WIDTH + out] = bg;
        }
        continue;
      }

      const uint8_t b0 = p[y], b1 = p[PLANE_SIZE + y], b2 = p[2 * PLANE_SIZE + y];
      for (int k = k0, bit = bit0; k < k1; k++, bit++) {
        uint8_t pen = ((b0 >> bit) & 1) | (((b1 >> bit) & 1) << 1) | (((b2 >> bit) & 1) << 2);
        fb[k * OUT_WIDTH + out] = pen ? cpal[pen] : bg;
      }
    }
  }
}

// playfield strip while gliding: line k shows window native x + oy, so a
// strip spans up to two window tile columns. No sprites (unused in play).
inline void renderShifted(uint16_t *fb, int row, const uint8_t *charram, const VideoState &v,
                          const VideoState &old, const ScrollView &s, const uint16_t *pal) {
  const uint16_t *cpal = pal + (v.ccb & 1) * 8;
  const uint16_t bg = pal[BG_PEN];
  const int x_top = 8 * (LAST_STRIP - row) + 7 + s.oy; // window x of line 0

  for (int k = 0; k < 8;) {
    const int x = x_top - k;
    const int k1 = k + (x & 7) + 1 < 8 ? k + (x & 7) + 1 : 8;
    shiftedLines(fb, k, k1, 7 - (x & 7), tileOf(x), charram, v, old, s, cpal, bg);
    k = k1;
  }
}

} // namespace cbdash_video

#endif
