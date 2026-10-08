#ifndef HERO_GAME_H
#define HERO_GAME_H

#include <stdint.h>
#include "hero_room.h"

// H.E.R.O. game logic, one tick = one 2600 frame. Coordinates and rules are
// the original's (HERO.md "Hero movement", "Overscan rules"): x = playfield
// pixel of the sprite's left edge, y grows upwards, the hero spans y - 1
// (feet sensor) .. y + 24 (head).
namespace hero_game {

struct Input {
  uint8_t up, down, left, right, fire;
};

struct Hero {
  uint8_t x, y;       // $9B, $9F
  uint8_t thrust;     // $E9, 0..$3F
  uint8_t facing;     // $B0: 0 right, 8 left
  uint8_t standing;   // $AC: $40 idle, $80 walking, 0 in the air
  uint8_t blocked;    // $E6: bit 7 ceiling, bit 6 floor (from the last tick)
  uint8_t turn_delay; // $FC
  uint8_t prop;       // $E7 propeller animation, 2..0
  uint8_t walk;       // $E8 walk animation, 4..0
};

struct Game {
  uint8_t level, screen; // $F5, $9C
  uint8_t furthest;      // $9E
  uint8_t frame;         // $81
  uint8_t start_delay;   // frames before the respawn glide starts
  uint8_t respawn;       // $AD: 0 playing, < $96 glide / wait
  uint8_t respawn_band;  // $AE
  uint8_t power;         // $AB
  uint8_t power_full;    // $AF
  uint8_t ball_x;        // ball wall, hero_room::NO_BALL = none ($A0 - 1)
  uint8_t prev_screen;   // $9D
  uint8_t saved_ball_x;  // $ED: state of the previous screen
  uint8_t dead;          // death started (sequence not modelled yet)
  Input last_in;         // input of the previous frame, used by its overscan
  hero_room::Room room;
  Hero hero;
};

static const uint8_t BAND_BOTTOM_Y = 21; // y of band 0's lower edge, band i spans
static const uint8_t BAND_H = 39;        // [21 + 39 i, 60 + 39 i)
static const uint8_t HERO_H = 24;

void startLevel(Game &g, uint8_t level);
// $F569: hero graphics page:frame for the renderer
void heroSprite(const Game &g, uint8_t &page, uint8_t &frame);
// One frame. The original runs the frame logic (bank 1: input, movement,
// screen changes) and then the overscan (bank 0: collisions, respawn,
// power); tick() runs the previous frame's overscan first, so the state
// after tick() matches a MAME RAM dump of that frame.
void tick(Game &g, const Input &in);

} // namespace hero_game

#endif
