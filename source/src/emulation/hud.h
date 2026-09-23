#ifndef HUD_H
#define HUD_H

#include <stdint.h>
#include "emulation.h"

#ifdef DEBUG_TIMING_FPS_HUD
// machine display flips, applied by Video::flip() via MADCTL after the frame
// buffer is sent
enum HudFlip : uint8_t {
  HUD_FLIP_NONE = 0,
  HUD_FLIP_Y    = 1, // 180 degree rotation
  HUD_FLIP_X    = 2, // column mirror
};

// draw emulation_fps centered at the physical bottom, if `row` holds it
void hud_render_fps(uint16_t *frame_buffer, uint16_t width, uint8_t row, uint8_t flip);
#endif

#endif // HUD_H
