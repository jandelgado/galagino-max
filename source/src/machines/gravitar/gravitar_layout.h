#ifndef GRAVITAR_LAYOUT_H
#define GRAVITAR_LAYOUT_H

#include "../bwidow/bwidow_layout.h"

// Gravitar on the panel: MAME visarea 0..420 x 0..400 (ROT0) scaled by
// 584 / 1024 onto 240 columns, rows 29..257 (aspect kept, centered). Beam
// center (210, 200), MAME avg_device::device_start.
namespace gravitar_layout {
static const bwidow_layout::Screen SCREEN = {{0, 0, 420, 400}, 210, 200, 584, 29};
// native HDMI: 1/16 units, 76 / 1024 a sixteenth (1.19 px a unit) onto
// NATIVE_WIDTH x 480 (rows 2..477), 1:1 pixels
static const bwidow_layout::Screen SCREEN_NATIVE = {{0, 0, 420 << bwidow_layout::NATIVE_FRAC, 400 << bwidow_layout::NATIVE_FRAC},
                                                    210 << bwidow_layout::NATIVE_FRAC, 200 << bwidow_layout::NATIVE_FRAC, 76, 2};
static const uint16_t NATIVE_WIDTH = 500;
} // namespace gravitar_layout

#endif
