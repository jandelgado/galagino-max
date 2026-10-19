#ifndef MHAVOC_LAYOUT_H
#define MHAVOC_LAYOUT_H

#include "../bwidow/bwidow_layout.h"

// Major Havoc on the panel: MAME visarea 0..300 x 0..260 (ROT0) scaled by
// 819 / 1024 onto 240 columns, rows 40..247 (aspect kept, centered). Beam
// center (150, 130), MAME avg_device::device_start.
namespace mhavoc_layout {
static const bwidow_layout::Screen SCREEN = {{0, 0, 300, 260}, 150, 130, 819, 40};

// Line::color = color RAM value << avg::TP_COLOR_SHIFT | level
static const uint8_t PALETTE_SIZE = 128;

// Line::color -> RGB565, byte swapped
void palette(uint16_t *pal);
} // namespace mhavoc_layout

#endif
