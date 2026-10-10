#ifndef GNG_HUD_H
#define GNG_HUD_H

#include <stdint.h>

// fg text columns of the 240 px panel. Native column 0 sits in the left
// crop, but the game draws Arthur's lives there (bottom left, 2 columns
// per life). The run of non-blank chars from native column 0 goes 8 px
// right; the blank column ending the run makes room:
//   native  |L L L L . . W W|   L = life, W = weapon frame
//   panel     L L L L . W W     (usual: L L L . . W W)
static const uint8_t GNG_FG_COLS = 30;

// non-blank chars from native column 0 on, at most GNG_FG_COLS. code and
// attr: one fg row, blank: bit set per transparent char (bank in attr b6-7)
static inline uint8_t gngLeadRun(const uint8_t *code, const uint8_t *attr, const uint32_t *blank) {
  uint8_t n = 0;
  for (; n < GNG_FG_COLS; n++) {
    const uint16_t c = code[n] + ((attr[n] & 0xc0) << 2);
    if (blank[c >> 5] & (1u << (c & 31))) {
      break;
    }
  }
  return n;
}

// native fg column shown at panel column col
static inline uint8_t gngFgColumn(uint8_t col, uint8_t lead) {
  return col < lead ? col : col + 1;
}

#endif
