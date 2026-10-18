#ifndef MISSILE_VIDEO_H
#define MISSILE_VIDEO_H

#include <stdint.h>

// Pure video helpers for missile. MAME atari/missile.cpp: get_bit3_addr,
// vram_mad_w/r, screen_update_missile, palette_w.
namespace missile_video {

constexpr int OUT_WIDTH = 240;
constexpr int CROP_LEFT = 8;    // native x 8..247 shown (crop to 240 columns)
constexpr int FIRST_LINE = 24;  // vblank lines 0..23; line 24 drawn black
constexpr int VIS_LINES = 232;  // lines 24..255
constexpr int FIRST_STRIP = 4;  // 29 strips: panel rows 32..263
constexpr int LINE_BYTES = 64;  // 256 px, 4 per byte
constexpr int BIT3_LINE = 224;  // lines 224..255 have a 3rd color bit

// 3rd color bit of pixel address (y << 8 | x), scattered over 0x200-0x5ff
inline uint16_t bit3Addr(uint16_t pixaddr) {
  return (uint16_t)(((pixaddr & 0x0800) >> 1) | ((~pixaddr & 0x0800) >> 2) |
                    ((pixaddr & 0x07f8) >> 2) | ((pixaddr & 0x1000) >> 12));
}

// 3rd bit access costs one extra CPU cycle
inline bool madExtraCycle(uint16_t addr) { return (addr & 0xe000) == 0xe000; }

// MADSEL write: 2 bits from data b7-6, 3rd bit from b5 on lines >= 224.
// wprom bit set = keep the old VRAM bit.
inline void madWrite(uint8_t *vram, const uint8_t *wprom, uint16_t addr, uint8_t data) {
  static const uint8_t LOOKUP[4] = {0x00, 0x0f, 0xf0, 0xff};
  uint8_t *d = vram + (addr >> 2);
  uint8_t mask = wprom[(addr & 7) | 0x10];
  *d = (uint8_t)((*d & mask) | (LOOKUP[data >> 6] & ~mask));
  if (madExtraCycle(addr)) {
    d = vram + bit3Addr(addr);
    mask = wprom[(addr & 7) | 0x18];
    const uint8_t bit = (uint8_t)-((data >> 5) & 1);
    *d = (uint8_t)((*d & mask) | (bit & ~mask));
  }
}

// MADSEL read: pixel bits in b7-5, other bits 1
inline uint8_t madRead(const uint8_t *vram, uint16_t addr) {
  uint8_t r = 0xff;
  const uint8_t v = vram[addr >> 2] & (uint8_t)(0x11 << (addr & 3));
  if (!(v & 0xf0)) {
    r &= ~0x80;
  }
  if (!(v & 0x0f)) {
    r &= ~0x40;
  }
  if (madExtraCycle(addr) && !(vram[bit3Addr(addr)] & (1 << (addr & 7)))) {
    r &= ~0x20;
  }
  return r;
}

// pen 0..7 of native pixel (x, y), MAME screen_update_missile
inline uint8_t pixel(const uint8_t *vram, int x, int y) {
  const uint8_t b = vram[y * LINE_BYTES + x / 4] >> (x & 3);
  uint8_t pix = (uint8_t)(((b >> 2) & 4) | ((b << 1) & 2));
  if (y >= BIT3_LINE) {
    pix |= (vram[bit3Addr((uint16_t)(y << 8)) + (x / 8) * 2] >> (x & 7)) & 1;
  }
  return pix;
}

// ~data b3 R, b2 G, b1 B. RGB565, bytes swapped for the panel.
inline uint16_t paletteColor(uint8_t data) {
  const uint16_t c = ((~data & 0x08) ? 0xf800 : 0) | ((~data & 0x04) ? 0x07e0 : 0) |
                     ((~data & 0x02) ? 0x001f : 0);
  return (uint16_t)((c >> 8) | (c << 8));
}

} // namespace missile_video

#endif
