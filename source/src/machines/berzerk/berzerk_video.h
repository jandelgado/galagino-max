#ifndef BERZERK_VIDEO_H
#define BERZERK_VIDEO_H

#include <stdint.h>

// Pure Berzerk video helpers (host tested): magic RAM ALU and shifter
// (MAME berzerk magicram_w), 256 -> 240 column map, palette.
namespace berzerk_video {

constexpr int OUT_WIDTH = 240;
constexpr int VIS_TOP = 32;     // first visible vpos
constexpr int VIS_LINES = 224;
constexpr int FIRST_STRIP = 4;  // 224 lines centered in 288
constexpr int CROP = 4;         // native x 0-3 and 252-255: always black
constexpr int MERGES = 8;       // OR-merged column pairs, 248 -> 240
constexpr int MERGE_FIRST = 19; // merge pair k near MERGE_FIRST + MERGE_STEP k
constexpr int MERGE_STEP = 31;

// 74181 in logic mode (M = 1): F of A, B with select s, then inverted as the
// board does. Per bit (MAME ttl74181_device::update):
//   F = !(!ap & bp), ap = !(a | b s0 | !b s1), bp = !(!b s2 a | a b s3)
inline uint8_t alu(uint8_t a, uint8_t b, uint8_t s) {
  const uint8_t s0 = (s & 1) ? 0xff : 0, s1 = (s & 2) ? 0xff : 0;
  const uint8_t s2 = (s & 4) ? 0xff : 0, s3 = (s & 8) ? 0xff : 0;
  const uint8_t not_ap = a | (b & s0) | (s1 & ~b);
  const uint8_t bp = ~((~b & s2 & a) | (a & b & s3));
  return not_ap & bp;
}

// shifter: data shifted towards LSB, MSBs filled from the previous write,
// bit reversed if ctrl bit 3 is set
inline uint8_t shift(uint8_t last, uint8_t data, uint8_t ctrl) {
  uint8_t v = (uint8_t)((((uint16_t)last << 8) | data) >> (ctrl & 7));
  if (!(ctrl & 8)) {
    return v;
  }
  v = (uint8_t)((v & 0xf0) >> 4 | (v & 0x0f) << 4);
  v = (uint8_t)((v & 0xcc) >> 2 | (v & 0x33) << 2);
  return (uint8_t)((v & 0xaa) >> 1 | (v & 0x55) << 1);
}

// first native x of merge pair k, kept inside one 4 px color group so both
// pixels share a color: 18, 50, 81, 112, 142, 174, 205, 236
constexpr int mergeStart(int k) {
  return MERGE_FIRST + MERGE_STEP * k - (((MERGE_FIRST + MERGE_STEP * k) & 3) == 3);
}

// native x -> output column, -1 = cropped. Merged pairs share a column,
// so a 1 px line on either one stays visible.
inline void buildColumnMap(int16_t map[256]) {
  int out = 0, k = 0;
  for (int x = 0; x < 256; x++) {
    if (x < CROP || x >= 256 - CROP) {
      map[x] = -1;
      continue;
    }
    map[x] = (int16_t)out;
    if (k < MERGES && x == mergeStart(k)) {
      k++;
      continue;
    }
    out++;
  }
}

// pen bits: 0 R, 1 G, 2 B, 3 intensity. Component 0, 108 or 255 (MAME
// Wells-Gardner resistor weights, measured from snapshots). RGB565, bytes
// swapped for the panel.
inline uint16_t pen(uint8_t c) {
  const uint8_t on = (c & 8) ? 255 : 108;
  const uint8_t r = (c & 1) ? on : 0, g = (c & 2) ? on : 0, b = (c & 4) ? on : 0;
  const uint16_t v = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
  return (uint16_t)((v >> 8) | (v << 8));
}

} // namespace berzerk_video

#endif
