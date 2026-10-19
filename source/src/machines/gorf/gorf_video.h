#ifndef GORF_VIDEO_H
#define GORF_VIDEO_H

#include <math.h>
#include <stdint.h>

// Pure Gorf render model (host tested): palette, 320 -> 288 row map, one
// output row from the 2bpp bitmap (MAME screen_update_astrocde, ROT270).
//
// Player view: column c = MAME line c (0..239), row = game row g (0 = top).
// Game pixel p (0..319, MAME x - 16) = 319 - g; byte p >> 2, bits 7-6 first.
// effy = c - TOP, active when 0 <= effy < vblank, else bgdata.
namespace gorf_video {

constexpr int OUT_W = 240, OUT_H = 288, GAME_ROWS = 320, BYTES = 80, LINES = 204, TOP = 22;
constexpr int GROUP = 10;  // game rows per merge group; GROUP - 1 output rows

// MAME astrocade_palette: 32 hues x 16 luma, index = color byte << 1 (luma
// = (byte & 7) << 1; odd luma only reached through sparkle). RGB565, bytes
// swapped for SPI.
inline void buildPalette(uint16_t pal[512]) {
  for (int color = 0; color < 32; color++) {
    const double angle = (color / 32.0) * (2.0 * M_PI);
    const float ry = color ? (float)(0.75 * sin(angle)) : 0;
    const float by = color ? (float)(1.15 * cos(angle)) : 0;
    for (int luma = 0; luma < 16; luma++) {
      const float y = luma / 15.0f;
      int c[3] = {(int)((ry + y) * 255),
                  (int)(((y - 0.299f * (ry + y) - 0.114f * (by + y)) / 0.587f) * 255),
                  (int)((by + y) * 255)};
      for (int i = 0; i < 3; i++) c[i] = c[i] < 0 ? 0 : (c[i] > 255 ? 255 : c[i]);
      const uint16_t v = (uint16_t)(((c[0] & 0xf8) << 8) | ((c[1] & 0xfc) << 3) | (c[2] >> 3));
      pal[color * 16 + luma] = (uint16_t)((v << 8) | (v >> 8));
    }
  }
}

// src[r] = game row (0 = top of player view), merge[r] = 1: also try row src[r] + 1.
// 10 game rows -> 9 output rows (the 9th reads rows 8 and 9): 32 merges.
inline void buildRowMap(uint16_t src[OUT_H], uint8_t merge[OUT_H]) {
  for (int r = 0; r < OUT_H; r++) {
    const int k = r / (GROUP - 1), i = r % (GROUP - 1);
    src[r] = (uint16_t)(k * GROUP + i);
    merge[r] = (i == GROUP - 2);
  }
}

struct Regs {
  uint8_t colors[8];  // ports 0-7; 0-3 right of the split, 4-7 left of it
  uint8_t split;      // colorsplit: 2 * (d & 0x3f), in bytes (4 px) from MAME x 16
  uint8_t bg;         // bgdata: ((d >> 6) & 3) * 0x55
  uint8_t vblank;     // game lines
  uint8_t sparkle[4]; // outlatch q2-q5; 0 = pixel value twinkles
};

// Replaces the luma of a color byte with a pseudo-random 4-bit value (MAME
// uses a 17-bit LFSR array per pixel position; this is a position + frame hash).
inline uint16_t sparkleColor(const uint16_t *pal, uint8_t colorByte, int col, int gameRow,
                             uint32_t frame) {
  const uint32_t h = ((uint32_t)(col * 7 + gameRow * 13) + frame * 29) * 0x9E3779B1u;
  return pal[((colorByte << 1) & 0x1f0) | (h >> 28)];
}

// One output row, columns 0..239. gameRow2 >= 0 (merged pair): where gameRow
// has pixel value 0, gameRow2's pixel and colors are taken. Sparkle pixels with
// value 0 are black; stars are drawn on top by the machine (see buildStars,
// sparkleColor).
inline void renderRow(const uint8_t *vram, const Regs &r, int gameRow, int gameRow2,
                      const uint16_t *pal, uint32_t frame, uint16_t *dst) {
  const int lines = r.vblank < LINES ? r.vblank : LINES;
  const int pa = 319 - gameRow, pb = gameRow2 >= 0 ? 319 - gameRow2 : 0;
  const int ba = pa >> 2, bb = pb >> 2;
  const int sa = 6 - 2 * (pa & 3), sb = 6 - 2 * (pb & 3);
  const uint8_t *ca = &r.colors[ba < r.split ? 4 : 0];
  const uint8_t *cb = &r.colors[bb < r.split ? 4 : 0];
  for (int c = 0; c < OUT_W; c++) {
    const int effy = c - TOP;
    const bool active = effy >= 0 && effy < lines;
    const uint8_t *row = active ? vram + effy * BYTES : vram;
    int pix = active ? (row[ba] >> sa) & 3 : (r.bg >> sa) & 3;
    uint8_t color = ca[pix];
    if (pix == 0 && gameRow2 >= 0) {
      const int p2 = active ? (row[bb] >> sb) & 3 : (r.bg >> sb) & 3;
      if (p2) {
        pix = p2;
        color = cb[p2];
      }
    }
    if (r.sparkle[pix] == 0)
      dst[c] = pix ? sparkleColor(pal, color, c, gameRow, frame) : pal[0];
    else
      dst[c] = pal[color << 1];
  }
}

// MAME sparklestar array: 17-bit LFSR, star where (shiftreg & 0xff) == 0xfe,
// index i = effy * 455 + x (455 = htotal). Stars in the visible game area
// only (MAME x 16..335, lines 0..239), packed (gameRow << 8) | column,
// sorted ascending. Draw where regs.sparkle[0] == 0 and the pixel is black.
template <class F>
inline void forEachStar(F f) {
  uint32_t sr = 0;
  for (uint32_t i = 0; i < (1u << 17) - 1; i++) {
    sr = (sr >> 1) | ((((sr >> 12) ^ ~sr) & 1u) << 16);
    if ((sr & 0xff) != 0xfe) continue;
    const uint32_t effy = i / 455, x = i % 455;
    if (effy >= 262 || x < 16 || x >= 336) continue;
    const uint32_t line = (effy + TOP) % 262;
    if (line >= (uint32_t)OUT_W) continue;
    f((319 - (x - 16)) << 8 | line);
  }
}

inline uint16_t starCount() {
  uint16_t n = 0;
  forEachStar([&n](uint32_t) { n++; });
  return n;
}

inline void buildStars(uint32_t *out) {
  uint16_t n = 0;
  forEachStar([&](uint32_t v) {
    uint16_t j = n++;
    while (j > 0 && out[j - 1] > v) {
      out[j] = out[j - 1];
      j--;
    }
    out[j] = v;
  });
}

}  // namespace gorf_video

#endif
