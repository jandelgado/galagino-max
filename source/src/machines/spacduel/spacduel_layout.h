#ifndef SPACDUEL_LAYOUT_H
#define SPACDUEL_LAYOUT_H

#include <stdint.h>
#include "../../emulation/vector2d.h"

// Space Duel on the panel: MAME visarea 0..540 x 0..400 (ROT0) scaled by
// 454 / 1024 onto 240 columns, rows 55..232 (aspect kept, centered).
namespace spacduel_layout {

// AVG flip, 0x0c00 bits 6 and 7 inverted (set here = flipped)
enum : uint8_t { FLIP_X = 0x40, FLIP_Y = 0x80 };

// Line::color = color111 << avg::SW_COLOR_SHIFT | level
static const uint8_t PALETTE_SIZE = 128;

// Clips avg::decode output (beam units) and maps it in place to panel
// coordinates. Returns the kept count.
uint16_t map(vec2d::Line *lines, uint16_t count, uint8_t flip);

// Line::color -> RGB565, byte swapped
void palette(uint16_t *pal);

} // namespace spacduel_layout

#endif
