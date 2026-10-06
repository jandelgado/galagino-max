#ifndef CCASTLES_VIDEO_H
#define CCASTLES_VIDEO_H

#include <stdint.h>

// Pure video helpers for ccastles. MAME atari/ccastles.cpp: paletteram_w,
// write_vram, bitmode_*, screen_update.
namespace ccastles_video {

constexpr int OUT_WIDTH = 240;
constexpr int CROP_LEFT = 8;      // native x 8..247 shown, edges hold no game content
constexpr int FIRST_LINE = 24;    // sync PROM: vblank lines 0..23
constexpr int VIS_LINES = 232;    // lines 24..255
constexpr int FIRST_STRIP = 4;    // 29 strips: panel rows 32..263
constexpr int SPRITE_BYTES = 160; // 40 sprites x 4
constexpr int LINE_BYTES = 128;   // 256 px, low nibble = left pixel

// 22k/10k/4.7k, 1k pulldown, scaled to 255 (MAME compute_resistor_weights)
constexpr uint8_t LEVEL[8] = {0, 32, 71, 104, 151, 184, 223, 255};

// addr = palette address & 0x3f, bit 5 = red MSB. All bits inverted.
// RGB565, bytes swapped for the panel.
inline uint16_t paletteColor(uint8_t addr, uint8_t data) {
  const uint8_t r = LEVEL[~(((data & 0xc0) >> 6) | ((addr & 0x20) >> 3)) & 7];
  const uint8_t g = LEVEL[~data & 7];
  const uint8_t b = LEVEL[~(data >> 3) & 7];
  const uint16_t c = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (uint16_t)((c >> 8) | (c << 8));
}

// final pen 0..31 from sprite pen mo (0x0f = none) and bitmap pixel
inline uint8_t mergePen(const uint8_t *priprom, uint8_t mo, uint8_t pix) {
  const uint8_t pr = priprom[0x40 | ((mo & 7) << 2) | ((mo & 8) >> 2) | ((pix & 8) >> 3)];
  const uint8_t pen = (pr & 2) ? mo : pix;
  return (uint8_t)(pen | ((pr & 1) << 4));
}

// DRAM write source: CPU address, or bitmode pixel register (/BITMD)
enum WriteMode : uint8_t { WRITE_DIRECT, WRITE_BITMODE };

// every DRAM write passes the write protect PROM, which picks the nibbles.
// pixba = bitmode X & 3 (0 for direct writes).
inline void writeVram(uint8_t *vram, const uint8_t *wpprom, uint16_t addr, uint8_t data,
                      WriteMode mode, uint8_t pixba) {
  uint8_t *dest = vram + (addr & 0x7ffe);
  const uint8_t wp = wpprom[(((addr & 0xf000) == 0) << 7) | ((addr & 0x0c00) >> 5) |
                            ((mode == WRITE_DIRECT) << 4) | ((addr & 1) << 2) | pixba];
  if (!(wp & 1)) {
    dest[0] = (dest[0] & 0xf0) | (data & 0x0f);
  }
  if (!(wp & 2)) {
    dest[0] = (dest[0] & 0x0f) | (data & 0xf0);
  }
  if (!(wp & 4)) {
    dest[1] = (dest[1] & 0xf0) | (data & 0x0f);
  }
  if (!(wp & 8)) {
    dest[1] = (dest[1] & 0x0f) | (data & 0xf0);
  }
}

// xy = bitmode X, Y latches
inline uint16_t bitmodeAddr(const uint8_t xy[2]) { return (uint16_t)((xy[1] << 7) | (xy[0] >> 1)); }

// latch1 = 6P outputs, bit n = Qn: Q0 /AX, Q1 /AY, Q2 /XINC, Q3 /YINC
inline void autoinc(uint8_t xy[2], uint8_t latch1) {
  if (!(latch1 & 1)) {
    xy[0] = (latch1 & 4) ? xy[0] - 1 : xy[0] + 1;
  }
  if (!(latch1 & 2)) {
    xy[1] = (latch1 & 8) ? xy[1] - 1 : xy[1] + 1;
  }
}

// sprite pens of lines y0..y0+7 into mo (prefilled 0x0f). Pen 7 transparent,
// later entries on top, clipped at x 255. Flip (cocktail only) not handled.
inline void drawSprites(uint8_t mo[8][256], int y0, const uint8_t *spriteram,
                        const uint32_t (*sprites)[16]) {
  for (int offs = 0; offs < SPRITE_BYTES; offs += 4) {
    const uint8_t *s = spriteram + offs;
    const int sy = 256 - 16 - s[1];
    if (sy + 16 <= y0 || sy >= y0 + 8) {
      continue;
    }

    const int sx = s[3];
    const int n = sx > 248 ? 256 - sx : 8;
    const uint8_t bank = (uint8_t)((s[2] >> 7) << 3);
    const uint32_t *g = sprites[s[0]];
    for (int l = 0; l < 8; l++) {
      const int r = y0 + l - sy;
      if (r < 0 || r >= 16) {
        continue;
      }
      uint32_t w = g[r];
      uint8_t *dst = mo[l] + sx;
      for (int i = 0; i < n; i++, w >>= 4) {
        const uint8_t p = w & 7;
        if (p != 7) {
          dst[i] = bank | p;
        }
      }
    }
  }
}

} // namespace ccastles_video

#endif
