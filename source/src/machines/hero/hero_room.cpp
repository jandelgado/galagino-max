#ifdef ENABLE_HERO

#include "hero_room.h"
#include "hero_data.h"

using namespace hero_data;

namespace hero_room {

// $F29D (bands), $F6A7 ($A0 ball, first visit), $F334 ($BE PF0), $D9F1
// (level start ball)
Room decode(uint8_t level, uint8_t screen) {
  static const uint8_t START_BALL = 0x3d; // $DA04 init block -> $A0 at level start
  const uint8_t r = firstRoom(level) + screen;
  Room room;

  Band &top = room.band[BAND_TOP];
  top.pf0 = 0xff;
  top.repeat = 0;
  if (screen == 0) {
    top.pf1 = 0xc0;
    top.pf2 = 0x00;
  } else if (screen >= 11) {
    top.pf1 = 0xff;
    top.pf2 = 0xff;
  } else { // previous room's bottom band
    top.pf1 = b1(ROOM_FB + r - 1);
    top.pf2 = b1(ROOM_FD + r - 1);
  }

  const uint8_t fe = b1(ROOM_FE + r);
  const uint8_t a0 = (fe & 0xfc) ? ((fe & 0xfc) | 1) : 0;
  const bool edge_ball = a0 == 0x09 || a0 == 0x99;

  Band &mid = room.band[BAND_MID];
  const bool open_edges = screen >= 10 || (minerScreen(level) != screen && edge_ball);
  mid.pf0 = open_edges ? 0x00 : 0xff; // $BE | $D9E3 (= 0)
  mid.pf1 = b1(ROOM_FA + r);
  mid.pf2 = b1(ROOM_FC + r);
  mid.repeat = fe & 1;

  Band &bottom = room.band[BAND_BOTTOM];
  bottom.pf0 = 0xff;
  bottom.pf1 = b1(ROOM_FB + r);
  bottom.pf2 = b1(ROOM_FD + r);
  bottom.repeat = 0;

  // first visit ($F6A7 on the frontier screen); a revisit keeps edge walls
  // only, see hero_game
  room.ball_x = screen == 0 ? START_BALL - 1 : (a0 ? a0 - 1 : NO_BALL);
  room.water = screen >= 10 || minerScreen(level) == screen;
  room.lava = (fe >> 1) & 1;
  room.moving_wall = level >= 13 && mid.pf1 == 0xff && mid.pf2 == 0x7f;
  room.hue = b1(LEVEL_HUE + (level & 3));
  return room;
}

uint64_t cells(const Band &b) {
  uint64_t left = 0; // bit c = cell c of the left half
  for (int i = 0; i < 4; i++) { left |= (uint64_t)((b.pf0 >> (4 + i)) & 1) << i; }
  for (int i = 0; i < 8; i++) { left |= (uint64_t)((b.pf1 >> (7 - i)) & 1) << (4 + i); }
  for (int i = 0; i < 8; i++) { left |= (uint64_t)((b.pf2 >> i) & 1) << (12 + i); }
  uint64_t right = 0;
  for (int c = 0; c < 20; c++) {
    const int from = b.repeat ? c : 19 - c;
    right |= ((left >> from) & 1) << c;
  }
  return left | (right << 20);
}

void step(uint8_t &level, uint8_t &screen, int dir) {
  if (dir > 0) {
    if (++screen >= screens(level)) {
      screen = 0;
      level = (level + 1) % LEVELS;
    }
  } else if (screen > 0) {
    screen--;
  } else {
    level = (level + LEVELS - 1) % LEVELS;
    screen = screens(level) - 1;
  }
}

} // namespace hero_room

#endif // ENABLE_HERO
