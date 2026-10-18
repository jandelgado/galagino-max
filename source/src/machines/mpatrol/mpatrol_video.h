#ifndef MPATROL_VIDEO_H
#define MPATROL_VIDEO_H

#include <stdint.h>

// Pure Moon Patrol (Irem M52) geometry, see MAME m52_v.cpp. Output column c = screen x - 136,
// output line l = screen y - 22 (visible area 240 x 252).
namespace mpatrol_video {

constexpr int OUT_WIDTH = 240;
constexpr int VIS_LINES = 252;
constexpr int FIRST_ROW = 16; // line 0 lands on galagino row 16 (strip 2)

// Custom chip on the scroll board, mangles the bg1xpos write.
inline uint8_t protection(uint8_t bg1xpos) {
  uint8_t n = 0;
  for (uint8_t v = bg1xpos & 0x7f; v; v >>= 1) n += v & 1;
  return n ^ (bg1xpos >> 7);
}

// Only tilemap lines 192-255 scroll; the rest is fixed at register value 255.
inline uint8_t charX(int col, uint8_t scroll, uint8_t tileY) {
  return (uint8_t)(col + 8 - (tileY >= 192 ? scroll : 0));
}

inline uint8_t charY(int line) { return (uint8_t)(line + 6); }

inline uint8_t bgImageX(int col, uint8_t bgx) { return (uint8_t)(col + 12 - bgx); }

// Image rows 0..63 bitmap, 64..255 solid pen 3, -1 = not covered.
inline int bgImageY(int line, uint8_t bgy) {
  int y = line + 6 - bgy;
  return (y < 0 || y > 255) ? -1 : y;
}

inline int spriteLine(uint8_t ram0) { return 235 - ram0; }
inline int spriteColumn(uint8_t ram3) { return ram3 - 7; }

} // namespace mpatrol_video

#endif
