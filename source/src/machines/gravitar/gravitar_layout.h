#ifndef GRAVITAR_LAYOUT_H
#define GRAVITAR_LAYOUT_H

#include "../bwidow/bwidow_layout.h"

// Gravitar on the panel: MAME visarea 0..420 x 0..400 (ROT0) scaled by
// 584 / 1024 onto 240 columns, rows 29..257 (aspect kept, centered). Beam
// center (210, 200), MAME avg_device::device_start.
namespace gravitar_layout {
static const bwidow_layout::Screen SCREEN = {{0, 0, 420, 400}, 210, 200, 584, 29};
} // namespace gravitar_layout

#endif
