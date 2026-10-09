#ifndef HERO_VIDEO_H
#define HERO_VIDEO_H

#include <stdint.h>
#include "hero_room.h"

// The display kernel: builds one 2600 scanline (160 colour indices = TIA
// colour >> 1) from the registers the game logic prepares each frame, and
// detects the TIA collisions of those lines in software. Source window:
// 160 x 192 lines, line 0 = MAME a2600 snapshot row 26. Output 240 x 288:
// x and y scaled by 1.5.
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
// HERO_TOP_LINE - y + k, graphics page:X + $19 - k, colour $DD8D - k. The
// kernel shows row Y on line 123 - Y, the pointers make Y = y + 2 the top.
static const int HERO_TOP_LINE = 121;
static const int HERO_ROWS = 26;
static const uint8_t NO_HERO = 0xff; // hero_page: not drawn

// band objects (P0): per band a first sprite (critter, lamp; graphics page
// $DB) and a second one (dynamite, explosion, miner, raft, tentacle; page
// $DA), colours in page $DD. Index 0 = bottom band, as in the original.
struct Sprite {
  uint8_t x;      // $A5,x / $A8,x
  uint8_t gfx;    // $CB,x / $D8,x
  uint8_t color;  // $CE,x / $DB,x
  uint8_t offset; // $A1,x: first sprite moved up by this many lines
};

// render input, prepared by the game logic once per frame
struct VideoState {
  hero_room::Band band[hero_room::BANDS]; // playfield, screen order (top first)
  uint8_t band_color[hero_room::BANDS];   // $C7, $C6, $C5
  uint8_t hue;                            // $A4: cap, floor and dark lines
  uint8_t background;                     // $E1: explosion flash
  uint8_t water;                          // floor background is water
  uint8_t water_color;                    // $DD01
  uint8_t wave;                           // $FB floor wave frame
  uint8_t ball;                           // $A0: wall at x $A0 - 1, 0 = none
  uint8_t ball_w;                         // ball and laser width (CTRLPF/NUSIZ1)
  Sprite obj[3], obj2[3];
  uint8_t p0_mirror;                      // REFP0 (miner on the left)
  uint8_t hero_x, hero_y, hero_mirror;
  uint8_t hero_page, hero_frame;          // graphics at hero_page:hero_frame ($91/$92)
  uint8_t laser_x;                        // M1 position, < 8 = hidden ($D6E8)
  // HUD ($D079, $D3A0)
  uint8_t power;                          // $AB
  uint8_t lives;                          // $B3
  uint8_t dynamite;                       // $B2
  uint8_t text[6];                        // glyph pointers into page $DF
  uint8_t text_color;
};

// TIA collision latches of one kernel pass (bit 7 set = hit), as the
// original stores them; index 0 = bottom band
struct Latches {
  uint8_t hero_obj[3];  // $D1-$D3: P1 vs first sprite (CXPPMM)
  uint8_t laser_obj[3]; // $D4-$D6: M1 vs first sprite (CXM1P)
  uint8_t laser_wall;   // $F4: bit 7 M1 vs playfield, bit 6 M1 vs ball
  uint8_t bottom_pp;    // CXPPMM after the kernel: P0 vs P1 in the bottom band
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
// P0 vs P1, M1 vs P0/PF/BL latches of the bands, hero_pf excluded
void collide(const VideoState &v, Latches &l);

} // namespace hero_video

#endif
