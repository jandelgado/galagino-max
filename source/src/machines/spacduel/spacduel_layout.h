#ifndef SPACDUEL_LAYOUT_H
#define SPACDUEL_LAYOUT_H

#include "../bwidow/bwidow_layout.h"

// Space Duel on the panel: MAME visarea 0..540 x 0..400 (ROT0) scaled by
// 454 / 1024 onto 240 columns, rows 55..232. Beam center (270, 200), MAME
// avg_device::device_start.
namespace spacduel_layout {
static const bwidow_layout::Screen SCREEN = {{0, 0, 540, 400}, 270, 200, 454, 55};
// native HDMI: 1/16 units, 75 / 1024 a sixteenth (1.17 px a unit) onto
// NATIVE_WIDTH x 480 (rows 5..473), 1:1 pixels. 76 would make 641 columns.
static const bwidow_layout::Screen SCREEN_NATIVE = {{0, 0, 540 << bwidow_layout::NATIVE_FRAC, 400 << bwidow_layout::NATIVE_FRAC},
                                                    270 << bwidow_layout::NATIVE_FRAC, 200 << bwidow_layout::NATIVE_FRAC, 75, 5};
static const uint16_t NATIVE_WIDTH = 634;
} // namespace spacduel_layout

#endif
