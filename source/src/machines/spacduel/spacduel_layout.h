#ifndef SPACDUEL_LAYOUT_H
#define SPACDUEL_LAYOUT_H

#include "../bwidow/bwidow_layout.h"

// Space Duel on the panel: MAME visarea 0..540 x 0..400 (ROT0) scaled by
// 454 / 1024 onto 240 columns, rows 55..232. Beam center (270, 200), MAME
// avg_device::device_start.
namespace spacduel_layout {
static const bwidow_layout::Screen SCREEN = {{0, 0, 540, 400}, 270, 200, 454, 55};
} // namespace spacduel_layout

#endif
