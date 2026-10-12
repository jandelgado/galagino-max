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

// Occlusion mask MO (MARBLE.md "Occlusion"), positioned relative to its owner's origin: the
// marble sprite top left (mx, my) or an object's (x, y). code: bit 15 hflip, 11-13 height - 1,
// 0-10 MO code. A mask pixel clears its owner's pixel (rule A), no other object's.
struct Mask {
  uint16_t code;
  int16_t dx, dy;
};
static const uint8_t MASKS = 16;  // pool shared by the marble and the objects

// Object sprite: frame id and palette set (MARBLE.md "Level object sprites"), origin in
// playfield pixels, its masks masks[mask .. mask + nmask). Level objects and the steelie
// frame: set 3, 5, 6, 7 of the race's sprite set (8 = wave strip shade). SET_DEATH: frame of
// the death set (player deaths, slinkies, acid, birds), palette marble_death_pal[set &
// SET_PAL] (0 player, SET_SHARDS shatter overlay, SET_SLINKY slinky, SET_ACID acid, SET_BIRD
// bird, SET_MINI + type - 7 minis), with SET_RACE5 the
// race's set 5 palette (shattering steelie); with SET_STAR sparkle star `frame` (one tile at x,
// y).
struct Obj {
  uint8_t frame, set;
  int16_t x, y;
  uint8_t mask, nmask;
};
static const uint8_t OBJS = 24;
static const uint8_t SET_DEATH = 0x80, SET_STAR = 0x40, SET_RACE5 = 0x20, SET_SHARDS = 1;
static const uint8_t SET_SLINKY = 2, SET_ACID = 3, SET_BIRD = 4, SET_MINI = 5, SET_PAL = 7;

// marble sprite top left relative to the player origin (sx, camera top + ARCADE_H - sy)
static const int16_t MARBLE_X = -8, MARBLE_Y = -16;

// render input, published by the emulation core once per frame (Seqlock)
struct VideoState {
  int16_t sx, sy;  // viewport origin in playfield pixels
  uint8_t level;
  uint8_t marble;  // enum Marble; HIDDEN also when every column is covered (+0x1c = 0)
  int16_t mx, my;  // marble sprite top left (MO 0x05) in playfield pixels
  uint8_t mask, nmask;  // marble masks in the pool
  Mask masks[MASKS];
  Obj objs[OBJS];  // object sprites, back to front
  uint8_t patch;   // last playfield patch, index into marble_patches (NO_PATCH = none)
  uint8_t n;       // objects
  uint8_t marbleAt;  // objects drawn before the marble, the rest on top
  uint8_t lift;    // tag 3 palette variant 0-3, 0xff none
};
static_assert(sizeof(VideoState) <= 320, "VideoState limit");

void clamp(VideoState &v, const marble_level::Level &l);

// 8 display lines of strip `row` (0..35), OUT_W pixels each, into dst: playfield, objects
// before marbleAt, marble and masks, the other objects
void renderRow(const marble_level::Level &l, const VideoState &v, int row, uint16_t *dst);

}  // namespace marble_video

#endif
