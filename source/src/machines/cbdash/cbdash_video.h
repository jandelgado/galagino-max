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

// what render reads from the emulated machine, latched at vblank
struct VideoState {
  uint8_t fg[FG_SIZE];     // c000: char codes + sprite list
  uint8_t color[FG_SIZE];  // c400: b0-1 = char code b8-9
  uint8_t palette[32];     // e000
  uint8_t ccb;             // e410: b0 char color, b1 sprite color
};

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

} // namespace cbdash_video

#endif
