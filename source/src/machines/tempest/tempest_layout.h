#ifndef TEMPEST_LAYOUT_H
#define TEMPEST_LAYOUT_H

#include <stdint.h>
#include "../../emulation/vector2d.h"

// Tempest on the panel. MAME avg_tempest_device swaps the beam axes and
// the game is ROT270, so beam (x, y) shows at screen (x - 5, 575 - y) on a
// 571 x 581 portrait screen. Scaled to fill 240 columns, rows 0..284 (the
// 3:4 tube is 11 % wider than on the monitor).
namespace tempest_layout {

// video invert bits, CPU 0x4000 bits 3 and 4
enum : uint8_t { FLIP_X = 0x08, FLIP_Y = 0x10 };

// Line::color = color RAM value << avg::TP_COLOR_SHIFT | level
static const uint8_t PALETTE_SIZE = 128;

// Clips avg::decode output (beam units) and maps it in place to panel
// coordinates. Returns the kept count.
uint16_t map(vec2d::Line *lines, uint16_t count, uint8_t flip);

// Line::color -> RGB565, byte swapped
void palette(uint16_t *pal);

} // namespace tempest_layout

#endif
