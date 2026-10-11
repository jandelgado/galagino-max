#include "marble_video.h"

#ifdef ENABLE_MARBLE

#include <string.h>
#include "../../emulation/asset.h"
#include "marble_rom.h"

namespace marble_video {

using namespace marble_level;

namespace {

// Player marble motion objects relative to MO 0x05, drawn in link order (later on top).
// MO screen position: x = MO x, top = -(MO y) - 256 - height * 8 (mod 512, MAME atarimo,
// yscroll 256). Traces: 0x05 at (sx + 16, sy + 16), 0x03 8 px right, 0x07 at MO y + 5.
struct Mo {
  uint8_t code;
  int8_t dx, dy;
  uint8_t h;  // tiles of 8 lines
};
static const Mo MARBLE_MOS[] = {{0x05, 0, 0, 2}, {0x03, 8, 0, 2}, {0x07, 0, 3, 1}};
static const int16_t MARBLE_H = 16;
static const int16_t MARBLE_W = 16;
static const uint8_t MO_TILES = sizeof(marble_mo_tiles) / sizeof(marble_mo_tiles[0]);

inline uint8_t reverse8(uint8_t b) {
  b = (uint8_t)((b >> 4) | (b << 4));
  b = (uint8_t)(((b & 0xcc) >> 2) | ((b & 0x33) << 2));
  return (uint8_t)(((b & 0xaa) >> 1) | ((b & 0x55) << 1));
}

// Hide mask of marble line ly (0..15): bit 31 - k set = marble pixel k is covered by a
// priority mask MO (pen 1 clears the marble pixel).
uint32_t hideMask(const VideoState &v, int ly) {
  uint32_t hide = 0;
  for (const Mask &e : v.masks) {
    if (!e.code) { break; }
    const int r = ly - e.dy;
    if (r < 0 || r >= (((e.code >> 11) & 7) + 1) * 8 || e.dx <= -8 || e.dx >= MARBLE_W) {
      continue;
    }
    const uint16_t t = marble_mask_index((e.code & 0x7ff) + (r >> 3));
    if (t == 0xffff) { continue; }
    uint8_t b = marble_mask_tiles[t][r & 7];
    if (e.code & 0x8000) { b = reverse8(b); }
    const uint32_t w = (uint32_t)b << 24;
    hide |= e.dx >= 0 ? w >> e.dx : w << -e.dx;
  }
  return hide;
}

// marble pixels of playfield line y, pen 0 transparent, clipped at the view edges
void drawMarble(const VideoState &v, int y, uint16_t *dst) {
  if (v.marble == HIDDEN || !v.visible || y < v.my || y >= v.my + MARBLE_H) { return; }
  const uint32_t hide = hideMask(v, y - v.my);

  for (const Mo &o : MARBLE_MOS) {
    const int ty = y - v.my - o.dy;
    if (ty < 0 || ty >= o.h * 8 || o.code >= MARBLE_MO_CODES) { continue; }
    const uint8_t first = marble_mo_code_first[o.code];
    if (first == 0xff || first + o.h > MO_TILES) { continue; }

    const uint8_t *px = marble_mo_tiles[first + (ty >> 3)] + (ty & 7) * 8;
    const int x0 = v.mx + o.dx - v.sx;
    for (int k = 0; k < 8; k++) {
      const int x = x0 + k;
      if (px[k] && x >= 0 && x < OUT_W && !(hide & (0x80000000u >> (o.dx + k)))) {
        dst[x] = marble_mo_palette[px[k]];
      }
    }
  }
}

}  // namespace

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
      drawMarble(v, y, dst);
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
    drawMarble(v, y, dst);
  }
}

}  // namespace marble_video

#endif  // ENABLE_MARBLE
