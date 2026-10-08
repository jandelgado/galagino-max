#ifndef HERO_ROOM_H
#define HERO_ROOM_H

#include <stdint.h>

// Room decode: room tables -> playfield bands (HERO.md "Room record").
namespace hero_room {

enum { BAND_TOP, BAND_MID, BAND_BOTTOM, BANDS };

struct Band {
  uint8_t pf0, pf1, pf2; // TIA playfield registers
  uint8_t repeat;        // 1: right half repeats the left, 0: mirrors it
};

struct Room {
  Band band[BANDS];
  uint8_t ball_x;      // ball: 8 px wall at x..x+7 over the bands, NO_BALL = none
  uint8_t water;       // floor background is water, floor waves animate
  uint8_t lava;        // middle band colour flashes ($FF3C)
  uint8_t moving_wall; // middle band PF2 animates ($FFB8), level >= 14
  uint8_t hue;         // TIA colour high nibble of the walls
};

static const uint8_t NO_BALL = 0xff;

Room decode(uint8_t level, uint8_t screen);

// 40 playfield cells of a band, bit c = pixels 4c..4c+3
uint64_t cells(const Band &b);

// room viewer: next (dir > 0) or previous screen, wraps across levels
void step(uint8_t &level, uint8_t &screen, int dir);

} // namespace hero_room

#endif
