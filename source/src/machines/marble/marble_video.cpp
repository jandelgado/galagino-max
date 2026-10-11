#include "marble_video.h"

#ifdef ENABLE_MARBLE

#include <string.h>

namespace marble_video {

using namespace marble_level;

void clamp(VideoState &v, const Level &l) {
  const int16_t maxy = l.rows * 8 - OUT_H;
  if (v.sx < 0) { v.sx = 0; }
  if (v.sx > PAN) { v.sx = PAN; }
  if (v.sy > maxy) { v.sy = maxy; }
  if (v.sy < 0) { v.sy = 0; }
}

void renderRow(const Level &l, const VideoState &v, int row, uint16_t *dst) {
  for (int line = 0; line < 8; line++, dst += OUT_W) {
    const int y = v.sy + row * 8 + line;
    if (y >= l.rows * 8) {
      memset(dst, 0, OUT_W * sizeof(uint16_t));
      continue;
    }
    const uint16_t *cells = l.map + (y >> 3) * COLS;
    const int ty = (y & 7) * 8;
    uint16_t *out = dst;
    int x = v.sx;
    int n = 0;
    while (n < OUT_W) {
      const uint16_t w = cells[x >> 3];
      const uint8_t *px = l.pixels[tileOf(w)] + ty;
      const uint16_t *pal = l.palette + setOf(w) * 32;
      int c = x & 7;
      const int end = (OUT_W - n < 8 - c) ? c + OUT_W - n : 8;
      if (flipOf(w)) {
        for (; c < end; c++) { *out++ = pal[px[7 - c]]; }
      } else {
        for (; c < end; c++) { *out++ = pal[px[c]]; }
      }
      n += end - (x & 7);
      x = (x & ~7) + end;
    }
  }
}

}  // namespace marble_video

#endif  // ENABLE_MARBLE
