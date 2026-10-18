#ifndef BZONE_TREADS_H
#define BZONE_TREADS_H

#include <stdint.h>

// Battle Zone has two 2-way sticks, one per tread (IN3 bits 0-3, active
// high). The 4-way stick drives both: up/down move, left/right spin in
// place, diagonals turn on one tread.
enum : uint8_t {
  BZ_RIGHT_BACK = 0x01, BZ_RIGHT_FWD = 0x02, BZ_LEFT_BACK = 0x04, BZ_LEFT_FWD = 0x08,
};

// needs BUTTON_* from input.h
static inline uint8_t bzone_treads(unsigned int k) {
  const int y = ((k & BUTTON_UP) ? 1 : 0) - ((k & BUTTON_DOWN) ? 1 : 0);
  const int x = ((k & BUTTON_RIGHT) ? 1 : 0) - ((k & BUTTON_LEFT) ? 1 : 0);
  int left = 0, right = 0; // -1 back, 0 stop, 1 forward
  if (y == 0) {
    left = x;
    right = -x;
  } else if (x == 0) {
    left = right = y;
  } else if ((x < 0) == (y > 0)) { // up+left, down+right
    right = y > 0 ? 1 : -1;
  } else { // up+right, down+left
    left = y > 0 ? 1 : -1;
  }
  return (left > 0 ? BZ_LEFT_FWD : 0) | (left < 0 ? BZ_LEFT_BACK : 0) |
         (right > 0 ? BZ_RIGHT_FWD : 0) | (right < 0 ? BZ_RIGHT_BACK : 0);
}

#endif
