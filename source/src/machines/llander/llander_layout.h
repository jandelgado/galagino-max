#ifndef LLANDER_LAYOUT_H
#define LLANDER_LAYOUT_H

#include <stdint.h>
#include "../../emulation/vector2d.h"
#include "../../emulation/dvg.h"

// Lunar Lander's text is 8x12 DVG units, 2.8 px at panel scale. This lays
// the picture out upright on the 240 x 288 panel and enlarges the text:
//
//   row   0  left HUD block (SCORE, TIME, FUEL, mission), 2.25x
//   row  50  playfield, native y 781..-19 at 15/64; messages and lander 2x
//   row 238  right HUD block (ALTITUDE, speeds, arrows), 2.25x
namespace llander_layout {

// Clips dvg::decode output and maps it in place to panel coordinates,
// enlarging text (lines tagged dvg::GLYPH, the tag is removed). Returns
// the kept count.
uint16_t map(vec2d::Line *lines, uint16_t count);

} // namespace llander_layout

#endif
