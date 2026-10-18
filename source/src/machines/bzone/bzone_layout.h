#ifndef BZONE_LAYOUT_H
#define BZONE_LAYOUT_H

#include <stdint.h>
#include "../../emulation/vector2d.h"
#include "../../emulation/avg.h"

// Battle Zone on the panel: screen x 32..543 (Mathbox clip window
// 36..543) and y 0..399 scaled by 15/32 onto 240 columns, rows 51..238.
// Half size text (HUD messages, HIGH SCORE) would be 2.8 px high; it is
// drawn 2x like the full size text, less where the run would leave the
// screen.
namespace bzone_layout {

// bzone.lay: red overlay above native y 80 -> panel row 88 = strip 11
static const uint8_t RED_STRIPS = 11;

// Clips avg::decode output and maps it in place to panel coordinates,
// enlarging half size text (lines tagged avg::GLYPH, the tag is removed).
// Returns the kept count.
uint16_t map(vec2d::Line *lines, uint16_t count);

} // namespace bzone_layout

#endif
