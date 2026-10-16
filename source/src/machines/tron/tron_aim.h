#ifndef TRON_AIM_H
#define TRON_AIM_H

#include <stdint.h>
#include "../../emulation/input.h"

// Tron aims with a spinner dial; galagino has none. The dial turns toward
// the stick direction instead, one aim step per frame, and holds when the
// stick is released. L1/R1 (galagino controller) turn it directly.
//
// The game (ROM 0x0289) reads 7 bit dial counts as deltas, 4 counts per aim
// step, into an angle 0-35 at RAM 0xc405 in 10 degree steps,
// counterclockwise in panel orientation (from the aim table at 0x4884 and
// the turret sprites):
//
//            9 (up)
//   18 (left)  +  0 (right)
//           27 (down)
class TronAim {
public:
  // once per frame; keys: BUTTON_* bits, angle: the game's aim angle
  void update(unsigned int keys, uint8_t angle) {
    if (keys & BUTTON_L1) {
      count += STEP;
      return;
    }
    if (keys & BUTTON_R1) {
      count -= STEP;
      return;
    }

    const int dx = ((keys & BUTTON_RIGHT) ? 1 : 0) - ((keys & BUTTON_LEFT) ? 1 : 0);
    const int dy = ((keys & BUTTON_UP) ? 1 : 0) - ((keys & BUTTON_DOWN) ? 1 : 0);
    if (!dx && !dy) {
      return;
    }

    // half steps (72 per turn): diagonals sit between two angles.
    // [dy + 1][dx + 1], dy up positive, dx right positive; centre unused
    static const int8_t DIRECTION[3][3] = {{45, 54, 63}, {36, 0, 0}, {27, 18, 9}};
    const int target = DIRECTION[dy + 1][dx + 1];
    int diff = (target - angle * 2 + HALF_STEPS) % HALF_STEPS;
    if (diff >= HALF_STEPS / 2) {
      diff -= HALF_STEPS;
    }
    if (diff >= -1 && diff <= 1) {
      return;
    }
    count += diff > 0 ? STEP : -STEP;
  }

  uint8_t dial() const { return count; }

private:
  static const int STEP = 4;  // dial counts per aim step
  static const int HALF_STEPS = 72;

  uint8_t count = 0;
};

#endif
