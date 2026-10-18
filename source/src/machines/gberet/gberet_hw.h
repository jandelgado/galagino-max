#ifndef GBERET_HW_H
#define GBERET_HW_H

#include <stdint.h>

// Pure Green Beret (Konami GX577) geometry and interrupt timer, see MAME konami/gberet.cpp.
// ROT0, visarea x 8..247, y 16..239: output column c = screen x - 8, output line
// l = screen y - 16.
namespace gberet_hw {

constexpr int OUT_WIDTH = 240;
constexpr int VIS_LINES = 224;
constexpr int FIRST_ROW = 32;       // line 0 lands on galagino row 32, centered in 288
constexpr int FIRST_TILE_ROW = 2;   // tile rows 2..29 visible
constexpr int VIS_TILE_ROWS = 28;
constexpr int VIS_TILES = VIS_TILE_ROWS * 64;
constexpr int SPRITE_BYTES = 0xc0;  // 48 sprites x 4 bytes
constexpr int TICKS_PER_FRAME = 16; // scanline timer every 16 of 256 lines
constexpr int VBLANK_TICK = 15;     // line 240

// Tick counter bits that go 0 -> 1 on this tick (MAME interrupt_tick, uint8_t math).
inline uint8_t tickRise(uint8_t ticks) { return (uint8_t)(~ticks & (ticks + 1)); }

// NMI on bit 0 if mask b0.
inline bool nmiTick(uint8_t ticks, uint8_t mask) { return tickRise(ticks) & mask & 0x01; }

// IRQ on bit 3 if mask b1 (mrgoemon) or bit 4 if mask b2 (gberet).
inline bool irqTick(uint8_t ticks, uint8_t mask) { return tickRise(ticks) & (mask << 2) & 0x18; }

inline int tileRow(int line) { return (line + 16) >> 3; }
inline int tileLine(int line) { return (line + 16) & 7; }

// e000 + row low byte, e020 + row bit 8
inline uint16_t rowScroll(const uint8_t *scrollram, int trow) {
  return (uint16_t)(scrollram[trow] | (scrollram[trow + 0x20] << 8));
}

// 64 x 8 px tilemap wraps at 512.
inline uint16_t tileX(int col, uint16_t scroll) { return (uint16_t)((col + 8 + scroll) & 511); }

// attr b7 is x bit 8, subtracted (MAME: sx = x - 2 * (attr & 0x80)).
inline int spriteColumn(uint8_t x, uint8_t attr) { return x - ((attr & 0x80) << 1) - 8; }
inline int spriteLine(uint8_t y) { return y - 16; }

} // namespace gberet_hw

#endif
