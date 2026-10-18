#ifndef HERO_DATA_H
#define HERO_DATA_H

#include <stdint.h>

// ROM data by original 2600 address (bank 0 $Dxxx, bank 1 $Fxxx), so the
// code reads like the disassembly (HERO.md). Only the blocks in hero_rom.h
// exist; other addresses are a bug. hero_rom.h is included by hero_data.cpp
// only, one copy in flash.
namespace hero_data {

uint8_t b0(uint16_t addr);
uint8_t b1(uint16_t addr);

static const uint8_t LEVELS = 20;

// bank 1 tables
static const uint16_t ROOM_F7 = 0xf700;      // [room] bottom band object
static const uint16_t ROOM_F8 = 0xf800;      // [room] middle band object
static const uint16_t ROOM_F9 = 0xf900;      // [room] top band object, bit 0 exit side
static const uint16_t ROOM_FA = 0xfa00;      // [room] middle band PF1
static const uint16_t ROOM_FB = 0xfb00;      // [room] bottom band PF1
static const uint16_t ROOM_FC = 0xfc00;      // [room] middle band PF2
static const uint16_t ROOM_FD = 0xfd00;      // [room] bottom band PF2
static const uint16_t ROOM_FE = 0xfe00;      // [room] flags
static const uint16_t FIRST_ROOM = 0xff00;   // [level]
static const uint16_t MINER_SCREEN = 0xff28; // [level]
static const uint16_t LAVA_COLOR = 0xff3c;   // [2] middle band lava colours
static const uint16_t LEVEL_HUE = 0xff6a;    // [level & 3]
static const uint16_t BAND_LUM = 0xffae;     // [bottom, mid, top]
// bank 0 tables
static const uint16_t FLOOR_WAVE = 0xda1d;   // [8] low byte of the floor wave in page $DC
static const uint16_t CAP_PF = 0xdc61;       // [x] top cap PF1/PF2 mask, x = 4..0
static const uint16_t CAP_PF0 = 0xdc66;      // [x] top cap PF0
static const uint16_t WATER_COLOR = 0xdd01;  // [level & 3]
static const uint16_t CAP_LUM = 0xdd09;      // [x] top cap, [1 + y] floor luminance
static const uint16_t DIGITS = 0xdf00;       // [d * 8 + 7 - row] 8 x 8 glyphs, upside down

inline uint8_t firstRoom(uint8_t level) { return b1(FIRST_ROOM + level); }
inline uint8_t screens(uint8_t level) {
  return (level + 1 < LEVELS ? firstRoom(level + 1) : 256) - firstRoom(level);
}
inline uint8_t minerScreen(uint8_t level) { return b1(MINER_SCREEN + level); }

} // namespace hero_data

#endif
