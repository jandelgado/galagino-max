#ifndef MARBLE_VIDEO_H
#define MARBLE_VIDEO_H

#include <stdint.h>
#include "marble_level.h"

// 240x288 viewport on the 288 px wide playfield, 1:1. sx pans 0..48, sy scrolls.
namespace marble_video {

static const int OUT_W = 240;
static const int OUT_H = 288;  // galagino renders 36 strips x 8 lines
static const int16_t PAN = marble_level::WIDTH - OUT_W;  // 48

// render input, published by the emulation core once per frame (Seqlock)
struct VideoState {
  int16_t sx, sy;  // viewport origin in playfield pixels
  uint8_t level;
};

void clamp(VideoState &v, const marble_level::Level &l);

// 8 display lines of strip `row` (0..35), OUT_W pixels each, into dst
void renderRow(const marble_level::Level &l, const VideoState &v, int row, uint16_t *dst);

}  // namespace marble_video

#endif
