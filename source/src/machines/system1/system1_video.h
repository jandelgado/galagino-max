#ifndef SYSTEM1_VIDEO_H
#define SYSTEM1_VIDEO_H

#include <stdint.h>
#include <string.h>

// Pure Sega System 1 video logic (MAME system1_v.cpp), shared by render and
// collision. x512 = MAME screen x (2x pixel clock), x = half-res native x,
// y = native line. ROT270 onto galagino: row = 271 - x, column = y.
namespace system1_video {

constexpr int VIS_X0 = 8;        // visarea 16..495 in 512 units
constexpr int VIS_X1 = 247;
constexpr int VIS_LINES = 224;
constexpr int FIRST_ROW = 24;    // 240 visible rows centered in 288
constexpr int LINE_X0 = 16;
constexpr int LINE_X1 = 495;
constexpr int SPRITES = 32;
constexpr int SPRITE_BYTES = 16;
constexpr int SPRITE_BANK = 0x8000;
constexpr int MAX_ROW_BYTES = 128;  // 512 units / 4 per byte: end of the line buffer
constexpr int PAGE_COLS = 32;
constexpr int PAGE_BYTES = 0x800;
constexpr int PENS_PER_SPRITE = 16;
constexpr uint8_t SPRITE_LIST_END = 0xff;
constexpr uint8_t TRANSPARENT_ROW_END = 0x0f;
constexpr uint8_t VIDEO_BLANK = 0x10;     // PPI port B bit 4
constexpr uint16_t SCROLL_X_LO = 0xffc;   // VRAM offsets
constexpr uint16_t SCROLL_X_HI = 0xffd;
constexpr uint16_t SCROLL_Y = 0xfbd;
constexpr int SCROLL_X_BIAS = 28;
constexpr uint8_t MIX_SOURCE = 0x03;      // lookup PROM: 0 sprite, 1 fg, 2/3 bg
constexpr uint8_t MIX_NO_COLLIDE = 0x04;
constexpr uint8_t MIX_BANK = 0x08;

inline int rowOfX(int x) { return VIS_X1 + FIRST_ROW - x; }
inline int xOfRow(int row) { return VIS_X1 + FIRST_ROW - row; }

inline int bgScroll(uint8_t lo, uint8_t hi) {
  return (int16_t)(lo | (hi << 8)) + SCROLL_X_BIAS;
}

// all four bg pages are page 0, so the 512 wide map repeats every 256
inline uint8_t bgCol(int x512, int scroll) { return (uint8_t)((x512 - scroll) / 2); }
inline uint8_t bgY(int y, uint8_t yscroll) { return (uint8_t)(y + yscroll); }

struct Tile {
  uint16_t code;
  uint8_t color;  // bits 6-7 = mixer priority
};

inline Tile tileAt(const uint8_t *page, int index) {
  const uint16_t d = page[2 * index] | (page[2 * index + 1] << 8);
  Tile t;
  t.code = ((d >> 4) & 0x800) | (d & 0x7ff);
  t.color = (d >> 5) & 0xff;
  return t;
}

// palette RAM index within the layer's 0x200 block
inline uint16_t tilePen(uint8_t color, uint8_t pix) { return ((color & 0x3f) << 3) | pix; }

inline uint8_t mixIndex(bool sprOpaque, uint8_t fgPix, uint8_t fgColor, uint8_t bgPix,
                        uint8_t bgColor) {
  return (sprOpaque ? 0 : 0x01) | (fgPix ? 0 : 0x02) | ((fgColor >> 6) << 2) |
         (bgPix ? 0 : 0x10) | ((bgColor >> 6) << 5);
}

// tiles are column-major: word = pixel column, native row 0 in bits 23..21
inline uint8_t tilePixel(const uint8_t *page, const uint32_t (*tiles)[8], uint16_t tileMask,
                         uint8_t x, uint8_t y, uint8_t &color) {
  const Tile t = tileAt(page, (y >> 3) * PAGE_COLS + (x >> 3));
  color = t.color;
  return (tiles[t.code & tileMask][x & 7] >> (3 * (7 - (y & 7)))) & 7;
}

struct Sprite {
  int16_t top;     // first line
  int16_t bottom;  // one past the last line
  int16_t x;       // 512 units
  uint16_t stride;
  uint16_t addr;   // row y reads from addr + (y - top + 1) * stride
  uint8_t bank;
};

// Sprites 0..n-1; entry[0] == 0xff hides it and all later ones.
inline int decodeSprites(const uint8_t *ram, uint8_t banks, Sprite *out) {
  for (int n = 0; n < SPRITES; n++) {
    const uint8_t *e = ram + n * SPRITE_BYTES;
    if (e[0] == SPRITE_LIST_END) {
      return n;
    }

    Sprite &s = out[n];
    s.top = e[0] + 1;
    s.bottom = e[1] + 1;
    s.x = (e[2] | (e[3] << 8)) & 0x1ff;
    s.stride = e[4] | (e[5] << 8);
    s.addr = e[6] | (e[7] << 8);
    s.bank = (((e[3] & 0x80) >> 7) | ((e[3] & 0x40) >> 5) | ((e[3] & 0x20) >> 3)) % banks;
  }
  return SPRITES;
}

inline uint16_t rowAddr(const Sprite &s, int y) {
  return (uint16_t)(s.addr + (y - s.top + 1) * s.stride);
}

// half-res x of pixel 0, sampled at even 512 positions like the tile layers
inline int spriteX(int x512) { return (x512 + 1) >> 1; }

// Calls px(i, nibble) for each pixel of one row until a 0x0f nibble. Pixel i
// covers 512 units x + 2i and x + 2i + 1. Returns the pixel count.
template <class F> inline int walkRow(const uint8_t *bank, uint16_t addr, F px) {
  const int delta = (addr & 0x8000) ? -1 : 1;
  uint16_t cur = addr;
  int i = 0;
  for (int n = 0; n < MAX_ROW_BYTES; n++, cur += delta) {
    const uint8_t d = bank[cur & 0x7fff];
    const bool mirrored = cur & 0x8000;
    const uint8_t first = mirrored ? (d & 0x0f) : (d >> 4);
    const uint8_t second = mirrored ? (d >> 4) : (d & 0x0f);
    if (first == TRANSPARENT_ROW_END) {
      return i;
    }
    px(i++, first);

    if (second == TRANSPARENT_ROW_END) {
      return i;
    }
    px(i++, second);
  }
  return i;
}

} // namespace system1_video

#endif
