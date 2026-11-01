#ifndef BWIDOW_GAME_LAYOUT_H
#define BWIDOW_GAME_LAYOUT_H

#include "bwidow_layout.h"

// Black Widow on the panel: MAME visarea 0..480 x 0..440 (ROT0) scaled by
// 511 / 1024 onto 240 columns, rows 34..253 (aspect kept, centered). Beam
// center (240, 220), MAME avg_device::device_start.
namespace bwidow_game_layout {
static const bwidow_layout::Screen SCREEN = {{0, 0, 480, 440}, 240, 220, 511, 34};
// native HDMI: 1/16 units, 69 / 1024 a sixteenth (1.08 px a unit) onto
// NATIVE_WIDTH x 480 (rows 2..476), 1:1 pixels
static const bwidow_layout::Screen SCREEN_NATIVE = {{0, 0, 480 << bwidow_layout::NATIVE_FRAC, 440 << bwidow_layout::NATIVE_FRAC},
                                                    240 << bwidow_layout::NATIVE_FRAC, 220 << bwidow_layout::NATIVE_FRAC, 69, 2};
static const uint16_t NATIVE_WIDTH = 518;
} // namespace bwidow_game_layout

#endif
