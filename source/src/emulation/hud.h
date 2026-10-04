#ifndef HUD_H
#define HUD_H

#include <stdint.h>
#include "../config.h"
#include "emulation.h"

#if defined(DEBUG_TIMING_FPS_HUD) || defined(BOOT_SELFTEST)
// machine display flips, applied by Video::flip() via MADCTL after the frame
// buffer is sent
enum HudFlip : uint8_t {
  HUD_FLIP_NONE = 0,
  HUD_FLIP_Y    = 1, // 180 degree rotation
  HUD_FLIP_X    = 2, // column mirror
};

// RGB565, byte swapped like the frame buffer
static constexpr uint16_t HUD_WHITE = 0xffff;
static constexpr uint16_t HUD_RED   = 0x00f8;
static constexpr uint16_t HUD_GRAY  = 0x1084; // RGB565 0x8410

// 8x8 font: 0-9, A-Z (lower case maps to upper), '-' and '.'.
// other chars render as space
static constexpr uint8_t HUD_GLYPH_SIZE = 8;

// draw `txt` with its top-left at physical (post flip) x,y. only the part
// inside strip `row` of the frame buffer is drawn, so call it for every row
void hud_text(uint16_t *frame_buffer, uint16_t width, uint8_t row, uint8_t flip,
              uint16_t x, uint16_t y, uint16_t color, const char *txt);
#endif

#ifdef DEBUG_TIMING_FPS_HUD
// draw emulation_fps centered at the physical bottom, if `row` holds it
void hud_render_fps(uint16_t *frame_buffer, uint16_t width, uint8_t row, uint8_t flip);
#endif

#endif // HUD_H
