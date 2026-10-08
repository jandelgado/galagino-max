#ifndef HERO_VIDEO_H
#define HERO_VIDEO_H

#include <stdint.h>
#include "hero_room.h"

// Builds one 2600 scanline (160 colour indices = TIA colour >> 1) and
// scales it to the panel. Source window: 160 x 192 lines, line 0 = MAME
// a2600 snapshot row 26. Output 240 x 288: x and y scaled by 1.5.
namespace hero_video {

static const int SRC_W = 160;
static const int SRC_H = 192;
static const int OUT_W = 240;
static const int OUT_H = 288;

// source lines (HERO.md "Display kernel")
static const int CAP_Y = 2;     // 5 lines, kernel x = 4..0 ($D12C)
static const int TOP_Y = 7;     // 40 lines, last one dark
static const int MID_Y = 47;    // 39 lines, first and last dark
static const int BOTTOM_Y = 86; // 39 lines, first dark
static const int FLOOR_Y = 125; // 4 lines of waves ($D351)
static const int HUD_Y = 129;   // 47 lines, grey ($D373)
static const int HUD_END = 176;

static const uint8_t HUD_COLOR = 0x04;
static const uint8_t BLACK = 0x00;

// hero sprite (P1, HERO.md "Hero movement"): 26 rows, row k at source line
// HERO_TOP_LINE - y + k, graphics page:X + $19 - k, colour $DD8D - k
static const int HERO_TOP_LINE = 119;
static const int HERO_ROWS = 26;
static const uint8_t NO_HERO = 0xff; // hero_page: not drawn

// render input, published once per frame by run_frame()
struct VideoState {
  hero_room::Room room;
  uint8_t level, screen; // shown in the HUD
  uint8_t frame;         // frame counter ($81) for lava and wave animation
  uint8_t ball_x;        // destructible wall, hero_room::NO_BALL = none
  uint8_t hero_x, hero_y, hero_mirror;
  uint8_t hero_page, hero_frame; // graphics at hero_page:hero_frame ($91/$92)
};

// render copy: the published state plus what prepare() derives from it
struct Frame {
  VideoState v;
  uint64_t cells[hero_room::BANDS]; // band playfield incl. the ball
};

// once per frame, after the Seqlock read
void prepare(Frame &f);
// dst: SRC_W palette indices for source line
void composeLine(const Frame &f, int line, uint8_t *dst);
// SRC_W indices -> OUT_W display pixels
void scaleLine(const uint8_t *src, uint16_t *dst);
inline int sourceLine(int out_row) { return out_row * 2 / 3; }

} // namespace hero_video

#endif
