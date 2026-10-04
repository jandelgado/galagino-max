#ifndef DVG_H
#define DVG_H

// Atari DVG (digital vector generator: Asteroids, Asteroids Deluxe, Lunar
// Lander), decoded at instruction level after MAME devices/video/avgdvg.cpp.
// Instead of steering a beam, a display list becomes a list of lines.

#include <stdint.h>
#include "vector2d.h"

namespace dvg {

// instruction budget per frame: ends garbage lists that loop forever
static const uint16_t MAX_STEPS = 4096;

// Decodes the display list starting at word 0 into lines in beam
// coordinates: x right, y up, 0..1023 on screen. Off-screen coordinates stay
// signed for the caller to clip. Line::color = intensity 1..15; beam moves
// (intensity 0) are skipped, zero length lines are dots. Stops at HALT,
// after max lines or MAX_STEPS instructions. Returns the line count.
//   ram: vector RAM, DVG words 0x000-0x3ff (2 KiB)
//   rom: vector ROM, DVG words 0x800-0xbff (2 KiB)
uint16_t decode(const uint8_t *ram, const uint8_t *rom, vec2d::Line *out, uint16_t max);

} // namespace dvg

#endif
