#ifndef GOTTLIEB_VIDEO_H
#define GOTTLIEB_VIDEO_H

#include <stdint.h>
#include <string.h>

// Gottlieb rev. 1 video, Q*bert and Krull (MAME gottlieb.cpp screen_update / draw_sprites), host
// tested: one 32 x 32 tilemap, tiles from ROM (Q*bert) or char RAM (Krull), 64 sprites of
// 16 x 16, 16 pens from palette RAM. Bit 0 of the 0x5803 latch puts the
// background over the sprites (Q*bert dropping behind the pyramid).
//
// ROT270, native 256 x 240 to 240 x 256 in the panel's 288 lines:
//   panel column = native y, panel line = 271 - native x
// so a panel line is one native x column: one tile word (8 pens) per tile
// row, one or two sprite words per sprite.
namespace gottlieb_video {

constexpr int WIDTH = 240;          // panel columns = native y 0..239
constexpr int TOP = 16;             // first panel line (native x 255)
constexpr int NATIVE_W = 256;
constexpr int TILE_ROWS = 30;
constexpr int MAP_W = 32;
constexpr int PENS = 16;
constexpr int SPRITES = 64;
constexpr int SPRITE_SIZE = 16;
constexpr int SPRITE_CLIP_X = 8;    // draw_sprites: native x 0..7 never shows sprites
constexpr int SPRITE_DX = 4, SPRITE_DY = 13;
constexpr uint8_t CTRL_BG_OVER_SPRITES = 0x01;

struct Sprite {
  int16_t x, y;   // native position of the top left pixel
  uint8_t code;
};

// RGB565, bytes swapped for the panel
inline uint16_t pen565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t v = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
  return (uint16_t)(v << 8 | v >> 8);
}

// levels: the machine's 4-bit gun value -> 0..255 table (resistor network)
inline void buildPens(const uint8_t *pal, const uint8_t *levels, uint16_t *pens) {
  for (int i = 0; i < PENS; i++) {
    const uint8_t gb = pal[2 * i], r = pal[2 * i + 1];
    pens[i] = pen565(levels[r & 0x0f], levels[gb >> 4], levels[gb & 0x0f]);
  }
}

// sprites that can reach the visible area, in drawing order
inline int listSprites(const uint8_t *spr, Sprite *out) {
  int n = 0;
  for (int i = 0; i < SPRITES; i++) {
    const uint8_t *s = spr + 4 * i;
    const int x = s[1] - SPRITE_DX, y = s[0] - SPRITE_DY;
    if (x + SPRITE_SIZE <= SPRITE_CLIP_X || y + SPRITE_SIZE <= 0 || y >= WIDTH) {
      continue;
    }
    out[n++] = {(int16_t)x, (int16_t)y, (uint8_t)(s[2] ^ 0xff)};
  }
  return n;
}

// tile pens of native column x; transparent: skip pen 0
// vram: 3800-3bff tile codes; tile index = code & codeMask
inline void drawTiles(int x, const uint8_t *vram, const uint16_t *pens, const uint32_t (*tiles)[8],
                      uint8_t codeMask, bool transparent, uint16_t *line) {
  const uint8_t *codes = vram + (x >> 3);
  for (int ty = 0; ty < TILE_ROWS; ty++) {
    uint32_t w = tiles[codes[ty * MAP_W] & codeMask][x & 7];
    uint16_t *dst = line + ty * 8;
    for (int k = 0; k < 8; k++, w >>= 4) {
      const uint8_t pen = w & 0x0f;
      if (pen || !transparent) {
        dst[k] = pens[pen];
      }
    }
  }
}

inline void drawSprites(int x, const uint16_t *pens, const uint32_t (*sprites)[32], const Sprite *list,
                        int n, uint16_t *line) {
  if (x < SPRITE_CLIP_X) {
    return;
  }
  for (int i = 0; i < n; i++) {
    const Sprite &s = list[i];
    const int col = x - s.x;
    if (col < 0 || col >= SPRITE_SIZE) {
      continue;
    }
    for (int h = 0; h < 2; h++) {
      uint32_t w = sprites[s.code][col * 2 + h];
      for (int k = 0, y = s.y + h * 8; k < 8; k++, y++, w >>= 4) {
        const uint8_t pen = w & 0x0f;
        if (pen && y >= 0 && y < WIDTH) {
          line[y] = pens[pen];
        }
      }
    }
  }
}

// strip `row` (8 panel lines) into fb (WIDTH x 8); ctrl: 0x5803 latch
inline void renderStrip(int row, const uint8_t *vram, uint8_t ctrl, const uint16_t *pens,
                        const uint32_t (*tiles)[8], uint8_t codeMask, const uint32_t (*sprites)[32],
                        const Sprite *list, int n, uint16_t *fb) {
  const bool bgOver = ctrl & CTRL_BG_OVER_SPRITES;
  for (int k = 0; k < 8; k++) {
    uint16_t *line = fb + k * WIDTH;
    const int x = TOP + NATIVE_W - 1 - (row * 8 + k);
    if (x < 0 || x >= NATIVE_W) {
      memset(line, 0, WIDTH * sizeof(uint16_t));
      continue;
    }

    if (bgOver) {
      for (int c = 0; c < WIDTH; c++) {
        line[c] = pens[0];
      }
    } else {
      drawTiles(x, vram, pens, tiles, codeMask, false, line);
    }
    drawSprites(x, pens, sprites, list, n, line);
    if (bgOver) {
      drawTiles(x, vram, pens, tiles, codeMask, true, line);
    }
  }
}

}  // namespace gottlieb_video

#endif
