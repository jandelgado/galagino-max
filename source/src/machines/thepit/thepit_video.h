#ifndef THEPIT_VIDEO_H
#define THEPIT_VIDEO_H

#include <cstdint>

// The Pit video geometry on galagino's portrait screen (MAME ROT90):
//
//   native X 0-255           -> galagino line X + 16 (strips 2..33)
//   native Y 16-239 visible  -> galagino column 239 - Y
//
// Tile layers scroll per native column (= per galagino strip): tilemap
// Y = screen Y + scroll. Pure functions, host tested.
namespace thepit_video {

constexpr int WIDTH = 224;
constexpr int FIRST_ROW = 2;
constexpr int LAST_ROW = 33;
constexpr int TILE_SLOTS = 29;        // 28 tiles + 1 partial when scrolled
constexpr int SPRITE_MIN_LINE = 33;   // MAME sprite clip: native x >= 17
constexpr int BG_COLOR_BASE = 32;     // solid background pens 32-39

// Color RAM: b0-2 char palette, b4-6 solid background color, b7 = no priority
inline uint8_t charColor(uint8_t cram) { return cram & 7; }
inline uint8_t bgColor(uint8_t cram) { return (cram >> 4) & 7; }

// Priority tiles: solid background drawn over chars and low priority sprites
inline bool isPriority(uint8_t cram) {
  return bgColor(cram) != 0 && !(cram & 0x80);
}

// VRAM index of tile slot `slot` (-1..27) in strip `row`. Slot s covers
// columns 8s + (scroll & 7) .. +7; slot -1 is the partial left tile.
inline uint16_t tileAddr(int row, int slot, uint8_t scroll) {
  int ty = (29 - slot + (scroll >> 3)) & 31;
  return ty * 32 + (row - FIRST_ROW);
}

// x = leftmost column, y = top line, flip = SpriteFlip bits (spriteflip.h)
struct Sprite {
  int16_t x, y;
  uint8_t code, color, flip, high;
};

// Decodes sprite RAM entry idx (4 bytes at s). false: not drawn.
inline bool decodeSprite(const uint8_t *s, int idx, Sprite &out) {
  if (s[0] == 0 || s[3] == 0) {
    return false;
  }

  uint8_t y = 240 - s[0];
  uint8_t x = s[3] + 1;
  if (idx < 4) {
    y++;   // MAME: sprites 0-3 are drawn one pixel down
  }

  out.x = 224 - y;
  out.y = x + 16;
  out.code = s[1] & 0x3f;
  out.flip = (s[1] >> 6) & 3;   // native flipx = rows, flipy = pixels
  out.color = s[2] & 7;
  out.high = (s[2] >> 3) & 1;

  return out.x > -16 && out.x < WIDTH && out.y + 16 > SPRITE_MIN_LINE;
}

}  // namespace thepit_video

#endif
