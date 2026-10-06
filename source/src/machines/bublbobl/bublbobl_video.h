#ifndef BUBLBOBL_VIDEO_H
#define BUBLBOBL_VIDEO_H

#include <stdint.h>

// Pure video geometry of MAME screen_update_bublbobl, host tested.
namespace bublbobl_video {

constexpr uint16_t OUT_WIDTH = 224; // 256 native columns, every 8th dropped
constexpr uint8_t VIS_TOP = 16;     // first visible native line
constexpr uint8_t STRIPS = 28;      // 224 visible lines / 8

// PROM entry bits (a71-25, 0x80 + type * 16 + yc / 2)
constexpr uint8_t PROM_SKIP = 0x08;
constexpr uint8_t PROM_SAME_COLUMN = 0x04;
constexpr uint8_t PROM_ROW_OFS = 0x03;

// output column -> native x, skips native x with (x & 7) == 7
constexpr uint8_t keep_x(uint16_t col) { return (uint8_t)((col / 7) * 8 + col % 7); }

// x before (chained) and after the object's first "new column" row
struct ObjPos {
  int16_t x_pre;
  int16_t x_set;
};

inline uint8_t first_new_column(const uint8_t *prom_types, uint8_t type) {
  for (uint8_t yc = 0; yc < 32; yc++) {
    const uint8_t e = prom_types[type * 16 + yc / 2];
    if (!(e & PROM_SKIP) && !(e & PROM_SAME_COLUMN)) {
      return yc;
    }
  }
  return 32;
}

inline int16_t object_x(const ObjPos &p, uint8_t first_new, uint8_t yc) {
  return yc >= first_new ? p.x_set : p.x_pre;
}

inline int16_t next_sx(const ObjPos &p, uint8_t first_new) {
  return (int16_t)((first_new < 32 ? p.x_set : p.x_pre) + 16);
}

// tile rows yc (tile at native line (sy + 8 * yc) & 0xff) that overlap
// native lines line0..line0+7, mod 256; 32 rows cover all 256 lines
inline uint8_t rows_for_strip(uint8_t sy, uint8_t line0, uint8_t yc[2]) {
  const uint8_t d = (uint8_t)(line0 - sy);
  yc[0] = d >> 3;
  if (!(d & 7)) {
    return 1;
  }
  yc[1] = (uint8_t)((yc[0] + 1) & 31);
  return 2;
}

} // namespace bublbobl_video

#endif
