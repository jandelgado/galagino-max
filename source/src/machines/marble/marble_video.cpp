#include "marble_video.h"

#ifdef ENABLE_MARBLE

#include <string.h>
#include "../../emulation/asset.h"
#include "marble_mem.h"
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
  if (v.marble == HIDDEN || y < v.my || y >= v.my + MARBLE_H) { return; }
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

// Wave strip shade set (priority MOs, MARBLE.md "Level object sprites")
static const uint8_t SET_SHADE = 8;

// playfield colour index (0..31) of playfield pixel (x, y)
inline uint8_t pfPixel(const Level &l, int x, int y) {
  if (y >= l.rows * 8) { return 0; }
  const uint16_t w = l.map[(y >> 3) * COLS + (x >> 3)];
  return l.pixels[tileOf(w)][(y & 7) * 8 + ((x & 7) ^ (flipOf(w) ? 7 : 0))];
}

// Level object sprite o on the 8 playfield lines from y0, into dst (8 lines of OUT_W). Columns
// off the strip or the view are skipped, tiles past the set are never read.
void drawObj(const Level &l, const VideoState &v, const Obj &o, int y0, uint16_t *dst) {
  const MarbleSpriteSet &s = marble_physics::spriteSet(v.level);
  if (o.frame >= s.nframes) { return; }
  const MarbleSpriteFrame &f = s.frames[o.frame];
  const bool shade = o.set == SET_SHADE;
  const uint16_t *pal = nullptr;
  if (!shade) {
    const uint8_t k = o.set == 3 ? 0 : o.set == 5 ? 1 : o.set == 6 ? 2 : 3;
    pal = marble_sprite_pal[v.level][k];
  }
  const uint16_t *lift = (o.set == 6 && v.lift < 4) ? marble_lift_pal[v.lift] : nullptr;
  const uint16_t *shades = marble_sprite_shade[v.level];

  for (uint8_t c = 0; c < f.ncols; c++) {
    const MarbleSpriteCol &col = s.cols[f.first + c];
    const int h = col.h & 0x7f;
    const int top = o.y + col.y;
    const int x0 = o.x + col.x - v.sx;
    if (top >= y0 + 8 || top + h * 8 <= y0 || x0 <= -8 || x0 >= OUT_W ||
        col.tile + h > s.ntiles) {
      continue;
    }
    const bool flip = (col.h & 0x80) != 0;
    const int a = top > y0 ? top : y0;
    const int b = top + h * 8 < y0 + 8 ? top + h * 8 : y0 + 8;
    for (int y = a; y < b; y++) {
      const int ty = y - top;
      const uint8_t *row = l.sprites[col.tile + (ty >> 3)] + (ty & 7) * 4;
      uint16_t *out = dst + (y - y0) * OUT_W;
      for (int k = 0; k < 8; k++) {
        const int x = x0 + k;
        if (x < 0 || x >= OUT_W) { continue; }
        const int tx = flip ? 7 - k : k;
        const uint8_t pen = (tx & 1) ? row[tx >> 1] & 15 : row[tx >> 1] >> 4;
        if (!pen) { continue; }
        if (shade) {  // pen 1 shows the playfield; 2 and 3 darken it, three colours survive
          if (pen == 1) { continue; }
          const uint8_t pf = pfPixel(l, x + v.sx, y) & 15;  // colour 0x300 + (pf << 4) + pen
          out[x] = (pf == 4 && pen == 3) ? shades[0]
                   : (pf == 15 && pen == 2) ? shades[1]
                   : (pf == 15 && pen == 3) ? shades[2] : 0;
        } else {
          out[x] = (lift && pen >= 1 && pen <= 4) ? lift[pen - 1] : pal[pen];
        }
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
  uint16_t *const strip = dst;
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

  // level objects behind the marble, the marble, objects in front (link order)
  const int y0 = v.sy + row * 8;
  const bool objs = l.sprites && v.level < LEVELS;
  const uint8_t n = v.n < OBJS ? v.n : OBJS;
  const uint8_t at = v.marbleAt < n ? v.marbleAt : n;
  if (objs) {
    for (uint8_t i = 0; i < at; i++) { drawObj(l, v, v.objs[i], y0, strip); }
  }
  for (int line = 0; line < 8; line++) { drawMarble(v, y0 + line, strip + line * OUT_W); }
  if (objs) {
    for (uint8_t i = at; i < n; i++) { drawObj(l, v, v.objs[i], y0, strip); }
  }
}

}  // namespace marble_video

#endif  // ENABLE_MARBLE
