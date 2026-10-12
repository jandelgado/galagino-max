#ifndef MARBLE_HUD_H
#define MARBLE_HUD_H

#include <stdint.h>
#include "marble_video.h"
#include "marble_mem.h"
#include "marble_flow.h"

// HUD (MARBLE.md "HUD"; model tools/marble_hud.py): the arcade alpha layer texts in a 30 x 36
// cell grid over the 240 x 288 view. The emulation core derives the Hud of each frame from the
// work RAM image and the flow sequencer (update, published in VideoState); the video core
// rebuilds the grid when it changes (prepare_frame) and blends one grid row over each
// rendered strip (render_row).
//
//   emulation core                   video core
//   update(tracker, flow, m, hud) -> build(grid, hud)  cells: alpha words, pal: RGB565
//                                    blend(grid, row)  8 lines over the strip, last (on top)
//
// Items keep the arcade alpha positions moved by (dx, dy): top items 3 columns left (timer 6,
// centred), banners and the final screen 6 left and 3 rows down; cells outside are clipped.
namespace marble_hud {

static const uint8_t ROWS = 36, COLS = 30;

enum Screen : uint16_t {
  PAL = 1 << 0,        // alpha palette loaded (else nothing shows: level load)
  TOP = 1 << 1,        // score, race timer
  INTRO = 1 << 2,      // race intro banner
  SILLY = 1 << 3,      // silly race banner
  BONUS = 1 << 4,      // race end bonus line
  BONUS_PAL = 1 << 5,  // 0x118d2 colours (black boxes)
  OUT = 1 << 6,        // out of time, game over
  FINAL = 1 << 7,      // final screen (playfield black)
  PUSH = 1 << 8,       // game over, wait for START
};

// alpha word: bits 0-9 code, 10-12 colour, 13 opaque (pen 0 drawn)
static const uint16_t OPAQUE = 0x2000;

struct Grid {
  uint16_t cells[ROWS][COLS];  // alpha words
  uint16_t pal[32];            // colour * 4 + pen, RGB565 (all 0 without PAL)
  uint8_t used[ROWS];          // row holds a cell
  marble_video::Hud shown;     // state the grid shows
  bool valid;
};

// emulation core: what update keeps between frames (marble_hud.py hud_states)
struct Tracker {
  uint16_t phase;     // 0x400390 of the last frame
  uint16_t since;     // frames since it changed (1 = first frame)
  uint16_t before;    // the phase before
  bool loaded;        // alpha palette loaded since the race init
  bool bonusDone;     // race end: the bonus is added (alpha off until the next init)
  uint32_t score;     // last frame's score
  int16_t base;       // intro: seconds before the first +5 s
  uint16_t screen;    // last frame's screen bits
  uint8_t pal3, pal11;
};

// HUD state of the frame just run (after the flow and the logic tick)
void update(Tracker &t, const marble_flow::Flow &f, const marble_physics::Mem &m,
            marble_video::Hud &h);

// rebuild g when h differs from the state it shows
void build(Grid &g, const marble_video::Hud &h);

// grid row `row` (strip of 8 lines, OUT_W pixels each) over dst
void blend(const Grid &g, int row, uint16_t *dst);

}  // namespace marble_hud

#endif
