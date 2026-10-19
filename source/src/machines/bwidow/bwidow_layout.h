#ifndef BWIDOW_LAYOUT_H
#define BWIDOW_LAYOUT_H

#include <stdint.h>
#include "../../emulation/vector2d.h"

// Black Widow board family (Space Duel, Gravitar) on the panel: the MAME
// visarea (ROT0) is scaled onto 240 columns, aspect kept, vertically
// centered.
namespace bwidow_layout {

// AVG flip (set here = flipped)
enum : uint8_t { FLIP_X = 0x40, FLIP_Y = 0x80 };

// Line::color = color111 << avg::SW_COLOR_SHIFT | level
static const uint8_t PALETTE_SIZE = 128;

struct Screen {
  vec2d::Rect visible;      // MAME visarea
  int16_t xcenter, ycenter; // AVG beam center, flip axis
  int32_t scale_mul;        // >> 10: visarea width onto 240 columns
  int16_t top_border;       // first panel row
};

// Clips avg::decode output (beam units) and maps it in place to panel
// coordinates. Returns the kept count.
uint16_t map(const Screen &screen, vec2d::Line *lines, uint16_t count, uint8_t flip);

// Line::color -> RGB565, byte swapped
void palette(uint16_t *pal);

} // namespace bwidow_layout

#endif
