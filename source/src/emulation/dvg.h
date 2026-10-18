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

struct Memory {
  const uint8_t *ram;          // vector RAM, words 0x000-0x3ff (2 KiB)
  const uint8_t *rom;          // vector ROM, words rom_base..rom_base + rom_words - 1
  uint16_t rom_base, rom_words;
  uint16_t glyph_lo, glyph_hi; // JSRL targets in [lo, hi) are text glyphs; lo == hi: none
};

static const uint8_t GLYPH = 0x80; // Line::color flag

// Decodes the display list starting at word 0 into lines in beam
// coordinates: x right, y up, 0..1023 on screen. Off-screen coordinates stay
// signed for the caller to clip. Line::color = intensity 1..15; beam moves
// (intensity 0) are skipped, zero length lines are dots. Stops at HALT,
// after max lines or MAX_STEPS instructions. Returns the line count.
// Lines drawn inside a glyph subroutine get color | GLYPH, and dx/dy hold
// the beam position at the first glyph of their run (consecutive glyph
// JSRLs at one call level), so callers can lay out text. dx/dy of other
// lines are left alone.
uint16_t decode(const Memory &mem, vec2d::Line *out, uint16_t max);

} // namespace dvg

#endif
