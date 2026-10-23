#ifndef MARIO_VIDEO_H
#define MARIO_VIDEO_H

#include <stdint.h>

// Pure geometry: 256 native columns onto the 240 column panel, host tested.
//
//   MARIO_CROP    native 8..247
//   MARIO_SCALE   native 0..255 without x & 15 == 15
//   MARIO_SCROLL  native pan..pan+239, pan follows player 1
namespace mario_video {

constexpr uint16_t OUT_WIDTH = 240;
constexpr uint8_t CROP_LEFT = 8;
constexpr uint8_t PAN_MAX = 16;          // 256 - OUT_WIDTH
constexpr uint8_t PAN_LEFT_EDGE = 64;    // player x left of this: pan 0
constexpr uint8_t PAN_RIGHT_EDGE = 192;  // right of this: PAN_MAX

constexpr uint8_t crop_x(uint16_t col) { return (uint8_t)(col + CROP_LEFT); }

constexpr uint8_t scale_x(uint16_t col) { return (uint8_t)((col / 15) * 16 + col % 15); }

// player 1 native x -> wanted pan, linear between the edges
constexpr uint8_t pan_target(uint8_t px) {
  return px < PAN_LEFT_EDGE ? 0
         : px > PAN_RIGHT_EDGE ? PAN_MAX
         : (uint8_t)((px - PAN_LEFT_EDGE) * PAN_MAX / (PAN_RIGHT_EDGE - PAN_LEFT_EDGE));
}

// one pixel per frame toward target, no jumps on wraparound
constexpr uint8_t pan_step(uint8_t pan, uint8_t target) {
  return pan < target ? (uint8_t)(pan + 1) : pan > target ? (uint8_t)(pan - 1) : pan;
}

} // namespace mario_video

#endif
