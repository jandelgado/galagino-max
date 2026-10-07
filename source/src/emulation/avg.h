#ifndef AVG_H
#define AVG_H

// Atari AVG (analog vector generator), Battle Zone and Star Wars variants,
// decoded at instruction level after MAME devices/video/avgdvg.cpp
// (avg_device, avg_bzone_device, avg_starwars_device). The state PROM fixes which handlers run per opcode;
// decode() runs that sequence directly. Like dvg, the display list
// becomes a list of lines instead of steering a beam.

#include <stdint.h>
#include "vector2d.h"

namespace avg {

// instruction budget per frame: ends garbage lists that loop forever
// (Star Wars attract mode needs about 3300)
static const uint16_t MAX_STEPS = 8192;

// Battle Zone: little endian, STAT sets intensity and clip window.
// Star Wars: big endian (6809), STAT sets 8 bit intensity and RGB color,
// no clip window.
enum Variant : uint8_t { BZONE, STARWARS };

struct Memory {
  const uint8_t *ram; // AVG bytes [0, ram_bytes), CPU 0x2000 up
  uint16_t ram_bytes;
  const uint8_t *rom; // AVG bytes [rom_base, rom_base + rom_bytes)
  uint16_t rom_base, rom_bytes;
  uint16_t glyph_lo, glyph_hi; // JSRL targets in [lo, hi) are text glyphs; lo == hi: none
  Variant variant;
};

static const uint8_t GLYPH = 0x80; // Line::color flag
// Star Wars Line::color: RGB bits 6-4 (R = 6), level 3-0 (1..14)
static const uint8_t SW_COLOR_SHIFT = 4;

// Registers that survive from one GO to the next (MAME keeps them in the
// device): beam, scale, STAT intensity, Battle Zone clip latches.
// Beam and clip in 16.16 screen units.
struct State {
  int32_t xcenter, ycenter;
  int32_t xpos, ypos;
  int32_t clipx_min, clipy_min, clipx_max, clipy_max;
  uint8_t scale, bin_scale, intensity, color;
  bool hst, lst; // clip switches, true = no clip corner pending
  void reset(int32_t xc, int32_t yc);
};

// Decodes the list at AVG address 0 into lines in screen units: x right,
// y down. Lines are clipped to the hardware window (MAME vg_flush:
// (0, 0)-(1280, 1280) until the list sets one). Line::color = intensity
// 1..15 (Star Wars: see SW_COLOR_SHIFT); moves (intensity 0) are skipped. Stops at HALT, after max lines
// or MAX_STEPS instructions. Returns the line count.
// Lines drawn inside a glyph subroutine get color | GLYPH, and dx/dy hold
// the beam position at the first glyph of their run, so callers can lay
// out text. A run is the glyph JSRLs at one call level; STAT/SCAL and
// vectors between them (scale changes, column moves) keep it going,
// anything else ends it. dx/dy of other lines are left alone.
uint16_t decode(const Memory &mem, State &s, vec2d::Line *out, uint16_t max);

} // namespace avg

#endif
