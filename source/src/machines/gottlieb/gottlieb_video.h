#ifndef GOTTLIEB_VIDEO_H
#define GOTTLIEB_VIDEO_H

#include <stdint.h>
#include <string.h>

// Gottlieb rev. 1 video, Q*bert and Krull (MAME gottlieb.cpp screen_update / draw_sprites), host
// tested: one 32 x 32 tilemap, tiles from ROM (Q*bert) or char RAM (Krull), 64 sprites of
// 16 x 16, 16 pens from palette RAM. Bit 0 of the 0x5803 latch puts the
// background over the sprites (Q*bert dropping behind the pyramid).
//
// ROT270 (Q*bert, Krull), native 256 x 240 to 240 x 256 in the panel's 288 lines:
//   panel column = native y, panel line = 271 - native x
// so a panel line is one native x column: one tile word (8 pens) per tile
// row, one or two sprite words per sprite.
//
// ROT0 (Tylz, *Rot0), native 256 x 240 to 240 x 240, window native x 4-243
// (content spans 8-243: sprites reach 243, tiles end at 239):
//   panel column = native x - 4, panel line = native y + 20
// Here tile and sprite words hold a native row instead of a column.
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

// Krull char RAM byte write (4000-4fff) into tile column words: tile = offset >> 5,
// row y = (offset >> 2) & 7, pixels x = 2 * (offset & 3) (high nibble) and x + 1 (low)
inline void charWrite(uint32_t (*tiles)[8], uint16_t offset, uint8_t v) {
  const uint32_t tile = offset >> 5, y = (offset >> 2) & 7, x = 2 * (offset & 3);
  const uint32_t shift = 4 * y;
  uint32_t *t = tiles[tile];
  t[x] = (t[x] & ~(0xfu << shift)) | (uint32_t)(v >> 4) << shift;
  t[x + 1] = (t[x + 1] & ~(0xfu << shift)) | (uint32_t)(v & 0x0f) << shift;
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

// ---- ROT0 ----

constexpr int ROT0_TOP = 20;        // first panel line (native y 0)
constexpr int ROT0_LEFT = 4;        // native x of panel column 0
constexpr int ROT0_TILE_COLS = (WIDTH + ROT0_LEFT + 7) / 8;   // 31, the last one partly

// sprites that can reach the visible area, in drawing order
inline int listSpritesRot0(const uint8_t *spr, Sprite *out) {
  int n = 0;
  for (int i = 0; i < SPRITES; i++) {
    const uint8_t *s = spr + 4 * i;
    const int x = s[1] - SPRITE_DX, y = s[0] - SPRITE_DY;
    if (x + SPRITE_SIZE <= SPRITE_CLIP_X || x >= ROT0_LEFT + WIDTH || y + SPRITE_SIZE <= 0 ||
        y >= TILE_ROWS * 8) {
      continue;
    }
    out[n++] = {(int16_t)x, (int16_t)y, (uint8_t)(s[2] ^ 0xff)};
  }
  return n;
}

// pens k0..k1-1 of one tile word to dst[k]; transparent: skip pen 0
inline void drawTileWord(uint32_t w, int k0, int k1, const uint16_t *pens, bool transparent, uint16_t *dst) {
  w >>= 4 * k0;
  for (int k = k0; k < k1; k++, w >>= 4) {
    const uint8_t pen = w & 0x0f;
    if (pen || !transparent) {
      dst[k] = pens[pen];
    }
  }
}

// tile pens of native row y: first and last tile cut by the window
inline void drawTileRow(int y, const uint8_t *vram, const uint16_t *pens, const uint32_t (*tiles)[8],
                        bool transparent, uint16_t *line) {
  const uint8_t *codes = vram + (y >> 3) * MAP_W;
  for (int tx = 0; tx < ROT0_TILE_COLS; tx++) {
    const uint32_t w = tiles[codes[tx]][y & 7];
    if (transparent && !w) {
      continue;
    }
    const int k0 = tx == 0 ? ROT0_LEFT : 0;
    const int k1 = tx == ROT0_TILE_COLS - 1 ? WIDTH + ROT0_LEFT - tx * 8 : 8;
    drawTileWord(w, k0, k1, pens, transparent, line + tx * 8 - ROT0_LEFT);
  }
}

// sprites: words [2 * row] (x 0-7) and [2 * row + 1] (x 8-15) of each sprite row
inline void drawSpriteRow(int y, const uint16_t *pens, const uint32_t (*sprites)[32], const Sprite *list, int n,
                          uint16_t *line) {
  for (int i = 0; i < n; i++) {
    const Sprite &s = list[i];
    const int row = y - s.y;
    if (row < 0 || row >= SPRITE_SIZE) {
      continue;
    }
    for (int h = 0; h < 2; h++) {
      uint32_t w = sprites[s.code][row * 2 + h];
      for (int x = s.x + h * 8; w; x++, w >>= 4) {
        const uint8_t pen = w & 0x0f;
        if (pen && x >= SPRITE_CLIP_X && x < ROT0_LEFT + WIDTH) {
          line[x - ROT0_LEFT] = pens[pen];
        }
      }
    }
  }
}

// strip `row` (8 panel lines) into fb (WIDTH x 8); ctrl: 0x5803 latch
inline void renderStripRot0(int row, const uint8_t *vram, uint8_t ctrl, const uint16_t *pens,
                            const uint32_t (*tiles)[8], const uint32_t (*sprites)[32], const Sprite *list, int n,
                            uint16_t *fb) {
  const bool bgOver = ctrl & CTRL_BG_OVER_SPRITES;
  for (int k = 0; k < 8; k++) {
    uint16_t *line = fb + k * WIDTH;
    const int y = row * 8 + k - ROT0_TOP;
    if (y < 0 || y >= TILE_ROWS * 8) {
      memset(line, 0, WIDTH * sizeof(uint16_t));
      continue;
    }

    if (bgOver) {
      for (int c = 0; c < WIDTH; c++) {
        line[c] = pens[0];
      }
    } else {
      drawTileRow(y, vram, pens, tiles, false, line);
    }
    drawSpriteRow(y, pens, sprites, list, n, line);
    if (bgOver) {
      drawTileRow(y, vram, pens, tiles, true, line);
    }
  }
}

}  // namespace gottlieb_video

#endif
