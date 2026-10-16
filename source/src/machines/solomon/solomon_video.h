#ifndef SOLOMON_VIDEO_H
#define SOLOMON_VIDEO_H

#include <stdint.h>

// Pure Solomon's Key geometry, MAME tecmo/solomon.cpp. ROT0, visarea
// x 0..255, y 16..239. Output line l = screen y - 16; output column c maps
// to native x c + CROP_LEFT (default), scaleX (SOLOMON_SCALE) or c + scroll
// (SOLOMON_SCROLL), HUD lines by hudTopX/hudBottomX.
namespace solomon_video {

constexpr int NATIVE_WIDTH = 256;
constexpr int OUT_WIDTH = 240;
constexpr int VIS_TOP = 16;          // first visible screen y
constexpr int VIS_LINES = 224;
constexpr int FIRST_LINE = 32;       // panel line of output line 0, centered in 288
constexpr int TILE_COLS = 32;
constexpr int FIRST_TILE_ROW = VIS_TOP / 8;
constexpr int VIS_TILE_ROWS = VIS_LINES / 8;
constexpr int VIS_TILES = VIS_TILE_ROWS * TILE_COLS;
constexpr int SPRITE_BYTES = 0x80;   // 32 sprites x 4 bytes
constexpr int SPRITE_SIZE = 16;
constexpr int SPRITE_Y_BASE = 241;   // MAME: sy = 241 - byte 2
constexpr int CROP_LEFT = 8;
constexpr int SCROLL_MAX = NATIVE_WIDTH - OUT_WIDTH;
constexpr int SCALE_GROUP = 15;      // output columns per 16 native columns
constexpr int BG_PALETTE = 128;

// HUD lines (crop and scroll) stay fixed and drop always empty native
// columns instead of the edges, which hold "1p" (x 0..15), the lives
// (x 0..) and the round digit (x 248..255).
constexpr int HUD_TOP_LINES = 8;     // tile row 2: 1p, score, Bonus
constexpr int HUD_BOTTOM = 208;      // tile rows 28-29: lives, Fairy, ROUND
constexpr int HUD_TOP_KEEP = 16;     // "1p", then drops x 16..23
constexpr int HUD_TOP_DROP = 8;
constexpr int HUD_BOTTOM_KEEP = 120; // drops x 120..135, between Fairy and the scroll

constexpr uint8_t hudTopX(int col) { return (uint8_t)(col < HUD_TOP_KEEP ? col : col + HUD_TOP_DROP); }
constexpr uint8_t hudBottomX(int col) { return (uint8_t)(col < HUD_BOTTOM_KEEP ? col : col + SCROLL_MAX); }

// SOLOMON_SCROLL: the window follows Dana, proportional to her x over the
// playfield (block columns x 8..232): left wall -> 0, right wall -> 16.
constexpr int PLAYER_SPRITE = 28 * 4;  // Dana always uses slot 28 (e070)
constexpr uint8_t PLAYER_COLOR = 6;
constexpr int PLAYER_X_MIN = 8;
constexpr int PLAYER_X_MAX = 232;

constexpr uint8_t scrollFor(int x) {
  return (uint8_t)(x <= PLAYER_X_MIN   ? 0
                   : x >= PLAYER_X_MAX ? SCROLL_MAX
                                       : (x - PLAYER_X_MIN) * SCROLL_MAX / (PLAYER_X_MAX - PLAYER_X_MIN));
}

// drops native x % 16 == 15: 16 columns, 256 -> 240
constexpr uint8_t scaleX(int col) { return (uint8_t)((col / SCALE_GROUP) * 16 + col % SCALE_GROUP); }

inline int spriteLine(uint8_t y) { return SPRITE_Y_BASE - y - VIS_TOP; }
inline uint16_t spriteCode(uint8_t code, uint8_t attr) { return (uint16_t)(code + 16 * (attr & 0x10)); }
inline uint8_t spriteColor(uint8_t attr) { return (attr & 0x0e) >> 1; }
inline uint16_t tileCode(uint8_t code, uint8_t attr) { return (uint16_t)(code + 256 * (attr & 0x07)); }
inline uint8_t tileColor(uint8_t attr) { return (attr & 0x70) >> 4; }

// xBGR_444 RAM pair (lo = GGGGRRRR, hi = xxxxBBBB) -> RGB565, bytes swapped for SPI
inline uint16_t paletteColor(uint8_t lo, uint8_t hi) {
  const uint8_t r = (uint8_t)((lo & 0x0f) * 0x11);
  const uint8_t g = (uint8_t)((lo >> 4) * 0x11);
  const uint8_t b = (uint8_t)((hi & 0x0f) * 0x11);
  const uint16_t c = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
  return (uint16_t)((c >> 8) | (c << 8));
}

} // namespace solomon_video

#endif
