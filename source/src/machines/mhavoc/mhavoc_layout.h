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

// AVG JSRL targets of the vector ROM text glyphs (avg::Memory glyph_lo/hi)
static const uint16_t GLYPH_LO = 0x1000, GLYPH_HI = 0x1228;

// Text lines (avg::GLYPH runs sharing one anchor, merged with runs next to
// them on the row) at most SMALL_TEXT beam units tall, about 5 panel rows,
// grow about their box center by up to TEXT_NUM / TEXT_DEN per axis, less
// where that would reach the visarea edge or half the gap to other text.
// Clears avg::GLYPH. avg::decode output, in place.
static const int16_t SMALL_TEXT = 7, TEXT_NUM = 3, TEXT_DEN = 2;
void enlarge_text(vec2d::Line *lines, uint16_t count);

// Line::color -> RGB565, byte swapped
void palette(uint16_t *pal);
} // namespace mhavoc_layout

#endif
