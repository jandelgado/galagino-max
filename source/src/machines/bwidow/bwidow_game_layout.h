#ifndef BWIDOW_GAME_LAYOUT_H
#define BWIDOW_GAME_LAYOUT_H

#include "bwidow_layout.h"

// Black Widow on the panel: MAME visarea 0..480 x 0..440 (ROT0) scaled by
// 511 / 1024 onto 240 columns, rows 34..253 (aspect kept, centered). Beam
// center (240, 220), MAME avg_device::device_start.
namespace bwidow_game_layout {
static const bwidow_layout::Screen SCREEN = {{0, 0, 480, 440}, 240, 220, 511, 34};
} // namespace bwidow_game_layout

#endif
