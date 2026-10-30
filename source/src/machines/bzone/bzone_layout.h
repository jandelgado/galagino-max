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

#ifdef HAL_VIDEO_NATIVE
// native HDMI: avg::Memory::frac_bits, coordinates in 1/16 beam units, one
// scale onto NATIVE_WIDTH x 480 (square pixels)
static const uint8_t NATIVE_FRAC = 4;
static const uint16_t NATIVE_WIDTH = 614;
// red overlay above native y 80 -> row 96 = strip 12
static const uint8_t RED_STRIPS_NATIVE = 12;

// map() for native: text at its own size, the glyph tag removed
uint16_t map_native(vec2d::Line *lines, uint16_t count);
#endif

} // namespace bzone_layout

#endif
