#ifndef ATETRIS_VIDEO_H
#define ATETRIS_VIDEO_H

#include <stdint.h>

// Pure video helpers for atetris (host tested). MAME atetris.cpp:
// get_tile_info, gfx_8x8x4_packed_msb, palette RGB_332.
namespace atetris_video {

constexpr int VIS_COLS = 42;    // 336 px
constexpr int VIS_ROWS = 30;    // 240 lines
constexpr int CELL_BYTES = 2;
constexpr int RAM_COLS = 64;    // tilemap row stride in cells
constexpr int OUT_WIDTH = 240;
constexpr int FIRST_STRIP = 3;  // 240 lines centered in 288

// castle top, only present in the game layout (MAME, 66 of 66 frames)
constexpr int GAME_TILE_COL = 19;
constexpr uint16_t GAME_TILE_CODE = 677;

// how 336 native columns fit 240
enum Fit : uint8_t {
  FIT_SCALE, // drop x % 7 == 2 or 5 (336 * 5 / 7 = 240)
  FIT_CROP,  // native x 0..239: player 1 side of the game layout
};

// native pixels 8 tx + i with bit i of keep set go to columns out, out+1, ...
struct Span {
  uint8_t keep;
  uint8_t out;
};

inline void buildSpans(Fit fit, Span s[VIS_COLS]) {
  int out = 0;
  for (int tx = 0; tx < VIS_COLS; tx++) {
    s[tx].out = (uint8_t)out;
    uint8_t keep = 0;
    for (int i = 0; i < 8; i++) {
      const int x = tx * 8 + i;
      if (fit == FIT_CROP ? x < OUT_WIDTH : (x % 7 != 2 && x % 7 != 5)) {
        keep |= 1 << i;
        out++;
      }
    }
    s[tx].keep = keep;
  }
}

inline uint16_t tileCode(const uint8_t *cell) { return cell[0] | ((cell[1] & 7) << 8); }
inline uint8_t tileColor(const uint8_t *cell) { return cell[1] >> 4; }

// row0 = first cell of tile row 0
inline bool isGameLayout(const uint8_t *row0) {
  return tileCode(row0 + GAME_TILE_COL * CELL_BYTES) == GAME_TILE_CODE;
}

// RGB 3-3-2 (MAME pal3bit/pal2bit) to RGB565, bytes swapped for the panel
inline uint16_t rgb332(uint8_t d) {
  const uint8_t r3 = d >> 5, g3 = (d >> 2) & 7, b2 = d & 3;
  const uint8_t r = (r3 << 5) | (r3 << 2) | (r3 >> 1);
  const uint8_t g = (g3 << 5) | (g3 << 2) | (g3 >> 1);
  const uint8_t b = b2 * 0x55;
  const uint16_t c = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (uint16_t)((c >> 8) | (c << 8));
}

} // namespace atetris_video

#endif
