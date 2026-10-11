#ifndef MARBLE_VIDEO_H
#define MARBLE_VIDEO_H

#include <stdint.h>
#include "marble_level.h"

// 240x288 viewport on the 288 px wide playfield, 1:1. sx pans 0..48, sy scrolls.
// The marble is drawn over the playfield (motion objects, MARBLE.md "Marble sprite").
namespace marble_video {

static const int OUT_W = 240;
static const int OUT_H = 288;  // galagino renders 36 strips x 8 lines
static const int16_t PAN = marble_level::WIDTH - OUT_W;  // 48
static const int16_t ARCADE_H = 240;                     // arcade screen lines
static const int16_t MARGIN = (OUT_H - ARCADE_H) / 2;    // view lines above the arcade screen

enum Marble : uint8_t { HIDDEN = 0, SHOWN = 1 };

// Occlusion mask MO (MARBLE.md "Occlusion"), positioned relative to the marble sprite:
// dx = x - mx, dy = (sy - y) + 16 - 8 * height. code: bit 15 hflip, 11-13 height - 1, 0-10 MO
// code; 0 ends the list.
struct Mask {
  uint16_t code;
  int16_t dx, dy;
};
static const uint8_t MASKS = 5;

// render input, published by the emulation core once per frame (Seqlock)
struct VideoState {
  int16_t sx, sy;  // viewport origin in playfield pixels
  uint8_t level;
  uint8_t marble;  // enum Marble; HIDDEN also when every column is covered (+0x1c = 0)
  int16_t mx, my;  // marble sprite top left (MO 0x05) in playfield pixels
  Mask masks[MASKS];
  uint8_t patch;  // last playfield patch, index into marble_patches (NO_PATCH = none)
};

void clamp(VideoState &v, const marble_level::Level &l);

// 8 display lines of strip `row` (0..35), OUT_W pixels each, into dst; marble on top
void renderRow(const marble_level::Level &l, const VideoState &v, int row, uint16_t *dst);

}  // namespace marble_video

#endif
