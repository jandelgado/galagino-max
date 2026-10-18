#ifndef SPRITEFLIP_H
#define SPRITEFLIP_H

#include <cstdint>

// Sprites are stored unflipped; blitters flip them at draw time instead of
// keeping pre-flipped copies in flash (and in the Arena once unpacked).
// "Rows" and "pixels" refer to the stored layout, which each machine maps to
// the screen its own way.

// Flip bits, same layout as the variant index of the former pre-flipped
// tables, so most machines pass `sprite.flags & 3` unchanged.
enum SpriteFlip : uint8_t {
  FLIP_NONE   = 0,
  FLIP_ROWS   = 1,  // reverse row order
  FLIP_PIXELS = 2,  // reverse pixel order within each row
};

// Reverses the order of the 16 2-bit pixels in a row word.
static inline uint32_t rev2bpp(uint32_t v) {
  v = (v >> 16) | (v << 16);
  v = ((v >> 8) & 0x00ff00ff) | ((v & 0x00ff00ff) << 8);
  v = ((v >> 4) & 0x0f0f0f0f) | ((v & 0x0f0f0f0f) << 4);
  v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
  return v;
}

// Reverses the order of the 8 4-bit pixels in a word.
static inline uint32_t rev4bpp(uint32_t v) {
  v = (v >> 16) | (v << 16);
  v = ((v >> 8) & 0x00ff00ff) | ((v & 0x00ff00ff) << 8);
  v = ((v >> 4) & 0x0f0f0f0f) | ((v & 0x0f0f0f0f) << 4);
  return v;
}

// Walks the rows of a 16x16 2bpp sprite (uint32_t[16], pixel 0 in the low
// bits) in flipped order.
class SpriteRows2bpp {
public:
  // flip: SpriteFlip bits
  SpriteRows2bpp(const uint32_t *spr, uint8_t flip)
    : row((flip & FLIP_ROWS) ? spr + 15 : spr),
      step((flip & FLIP_ROWS) ? -1 : 1),
      mirror(flip & FLIP_PIXELS) { }

  void skip(int n) { row += n * step; }

  uint32_t next() {
    uint32_t v = *row;
    row += step;
    return mirror ? rev2bpp(v) : v;
  }

private:
  const uint32_t *row;
  int step;
  bool mirror;
};

// Same for 16x16 4bpp sprites (uint32_t[32]): row r is words 2r (pixels
// 0-7) and 2r+1 (pixels 8-15), pixel 0 in the low nibble.
class SpriteRows4bpp {
public:
  // flip: SpriteFlip bits
  SpriteRows4bpp(const uint32_t *spr, uint8_t flip)
    : row((flip & FLIP_ROWS) ? spr + 30 : spr),
      step((flip & FLIP_ROWS) ? -2 : 2),
      mirror(flip & FLIP_PIXELS) { }

  void skip(int n) { row += n * step; }

  void next(uint32_t out[2]) {
    if (mirror) {
      out[0] = rev4bpp(row[1]);
      out[1] = rev4bpp(row[0]);
    } else {
      out[0] = row[0];
      out[1] = row[1];
    }
    row += step;
  }

private:
  const uint32_t *row;
  int step;
  bool mirror;
};

#endif
