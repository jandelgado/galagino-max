#ifndef HERO_GAME_H
#define HERO_GAME_H

#include <stdint.h>
#include "hero_room.h"
#include "hero_video.h"

// H.E.R.O. game logic, one tick = one 2600 frame. State, coordinates and
// rules are the original's (HERO.md), each field names its RAM address:
// x = playfield pixel of the sprite's left edge, y grows upwards, the hero
// spans y - 1 (feet sensor) .. y + 24 (head). Band index 0 = bottom.
namespace hero_game {

struct Input {
  uint8_t up, down, left, right, fire;
  uint8_t start;  // console RESET switch
  uint8_t select; // console SELECT switch: next game
};

struct Hero {
  uint8_t x, y;       // $9B, $9F
  uint8_t thrust;     // $E9, 0..$3F
  uint8_t facing;     // $B0: 0 right, 8 left
  uint8_t standing;   // $AC: $40 idle, $80 walking, 0 in the air
  uint8_t blocked;    // $E6: bit 7 ceiling, bit 6 floor
  uint8_t turn_delay; // $FC
  uint8_t prop;       // $E7 propeller animation, 2..0
  uint8_t walk;       // $E8 walk animation, 4..0
};

struct Game {
  uint8_t game;          // $80: game number 0..4, bit 7 = past level 20
  uint8_t frame;         // $81
  uint8_t random;        // $82
  uint8_t select_delay;  // $83: frames until SELECT steps again
  uint8_t joy;           // $84: SWCHA high nibble, active low
  uint8_t level, screen; // $F5, $9C
  uint8_t prev_screen;   // $9D
  uint8_t furthest;      // $9E
  uint8_t wall;          // $A0: ball wall at x - 1, 0 = none
  uint8_t obj_state[3];  // $A1-$A3: first sprite offset, GONE = none
  uint8_t hue;           // $A4: level hue, DARK = lamp out
  uint8_t obj_x[3];      // $A5-$A7
  uint8_t obj2_x[3];     // $A8-$AA: dynamite, miner, raft, tentacle; NONE
  uint8_t power;         // $AB
  uint8_t respawn;       // $AD: 0 playing, >= $96 dying, else glide / wait
  uint8_t respawn_band;  // $AE
  uint8_t power_full;    // $AF
  uint8_t wear;          // $B1: laser wear of the wall
  uint8_t dynamite;      // $B2
  uint8_t lives;         // $B3
  uint8_t idle;          // $B4: no input for $B4 * 2048 frames, bit 7 = blank
  uint8_t timer;         // $B5: bit 7 = no demo restart, bits 2-0 count to $B4
  uint8_t demo;          // $B6: attract demo, counts down every 8 frames
  uint8_t score[3];      // $B7-$B9 BCD, high byte first
  uint8_t over;          // $BA: no game running (game over, game select)
  uint8_t raft_ride;     // $BD (frame logic)
  uint8_t fuse[3];       // $DE-$E0
  uint8_t flash;         // $E1: background colour of the explosion flash
  uint8_t hero_pf[3];    // $E2-$E4: bit 7 P1 vs playfield, bit 6 P1 vs ball
  uint8_t osc[3];        // $EA-$EC: critter and raft motion, bit 7 = up
  uint8_t saved[5];      // $ED-$F1: $A0-$A4 of the previous screen
  uint8_t rescued;       // $F2
  uint8_t laser;         // $F3: beam phase, bit 7 = off
  uint8_t laser_hit;     // $F4: kernel M1 latches since the last restart
  uint8_t anim;          // $F6: critter animation
  uint8_t hit[3];        // $F7-$F9: critter dying timer
  uint8_t blast;         // $FA: wall blown up by this explosion
  uint8_t wall_phase;    // $FD: moving wall, bit 7 = stopped
  uint8_t laser_pos;     // M1 position for the next kernel ($D6E8)
  Hero hero;
  hero_video::Latches latch;  // collisions of the last kernel pass
  hero_video::VideoState view; // display registers ($BE-$DD, $91-$9A)
};

static const uint8_t GONE = 0x32;  // $A1,x
static const uint8_t NONE = 0x93;  // $A5,x / $A8,x
static const uint8_t DARK = 0x01;  // $A4
static const uint8_t BAND_BOTTOM_Y = 21; // y of band 0's lower edge, band i spans
static const uint8_t BAND_H = 39;        // [21 + 39 i, 60 + 39 i)
static const uint8_t HERO_HEIGHT = 24;

// $F000: power on, the game select screen for Game 1; RESET starts it
void powerOn(Game &g);
// RAM byte at address $80-$FF, nullptr where the remake keeps no state
uint8_t *ram(Game &g, uint8_t addr);
// One frame: the previous overscan's second half (bank 0, collisions,
// respawn, dynamite, power), the frame logic (bank 1: input, movement,
// objects, display registers), the kernel's collisions and the overscan's
// first half (animation, laser). The state after tick() matches a MAME
// RAM dump taken when the game increments $81.
void tick(Game &g, const Input &in);
// room viewer and tests: g shows level/screen on a first visit, critters
// alive, no movement
void viewRoom(Game &g, uint8_t level, uint8_t screen);

} // namespace hero_game

#endif
