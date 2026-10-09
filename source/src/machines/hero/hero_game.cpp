#ifdef ENABLE_HERO

#include "hero_game.h"
#include "hero_data.h"
#include <string.h>

using namespace hero_data;
using namespace hero_room;
using hero_video::Sprite;
using hero_video::VideoState;

namespace hero_game {

// joystick ($84, SWCHA high nibble, active low)
static const uint8_t JOY_RIGHT = 0x80;
static const uint8_t JOY_LEFT = 0x40;
static const uint8_t JOY_DOWN = 0x20;
static const uint8_t JOY_UP = 0x10;
static const uint8_t JOY_NONE = 0xf0;
static const uint8_t JOY_ONLY_DOWN = 0xd0;

static const uint8_t START_X = 0x20;          // $DA04 level start block
static const uint8_t START_Y = 0x8b;
static const uint8_t START_WALL = 0x3d;
static const uint8_t START_RESPAWN = 0x95;
static const uint8_t START_DYNAMITE = 6;
static const uint8_t START_LIVES = 4;         // $FF8A
static const uint8_t MAX_LIVES = 7;           // $DBF3
static const uint8_t DYING = 0x96;            // $AD >= this: death sequence
static const uint8_t RESPAWN_TOP_Y = 0x8c;    // $D7DC
static const uint8_t POWER_FILL_BELOW = 0x77; // $D9C7
static const uint8_t POWER_MAX = 0x51;
static const uint8_t THRUST_MAX = 0x3f;
static const uint8_t THRUST_HOVER = 0x1f;     // $D810
static const uint8_t TOP_EXIT_Y = 0x8a;       // $F1C0
static const uint8_t ENTER_TOP_Y = 0x89;      // $F1F8
static const uint8_t ENTER_BOTTOM_Y = 4;      // $F1C4
static const uint8_t WATER_Y = 0x16;          // $F1E0
static const uint8_t LEFT_EXIT_X = 0x0c;      // $F24B
static const uint8_t RIGHT_EXIT_X = 0x93;     // $F258
static const uint8_t TURN_DELAY = 3;          // $F230
static const uint8_t FACE_LEFT = 8;
static const uint8_t FUSE = 0x40;             // $D90E
static const uint8_t FUSE_BANG = 0x1f;        // $D94D
static const uint8_t HIT_TIME = 0x13;         // $D8A2
static const uint8_t EDGE_WALL_L = 0x09;      // $A0 of the screen edge walls
static const uint8_t EDGE_WALL_R = 0x99;
static const uint8_t RAFT_GFX = 0x97;         // $F4AF
static const uint8_t BLANK_GFX2 = 0x99;       // $F492
static const uint8_t HIT_GFX = 0x76;          // $F417
static const uint8_t DEEP = 11;               // screens >= this: water levels
static const uint8_t LEVEL_LOOP = 12;         // after level 20 ($D7AA)

static const uint8_t SPEED[8] = {2, 1, 0, 0, 0, 0, 1, 2};      // $FFB0
static const uint8_t RESPAWN_Y[4] = {0x22, 0x49, 0x70, 0x1c}; // $DAFC
static const uint8_t FLOOR_Y[3] = {0x3c, 0x63, 0xef};          // $D9E9
static const uint8_t CEILING_Y[3] = {0xef, 0x24, 0x4b};        // $D9EC
static const uint8_t NO_LASER_Y[4] = {0x76, 0x50, 0x29, 0x77}; // $D9E5
static const uint8_t BLAST_Y_LO[3] = {0x15, 0x3c, 0x63};       // $DBFC
static const uint8_t BLAST_Y_HI[3] = {0x1c, 0x43, 0x6a};       // $DAFF
static const uint8_t OSC_MAX[3] = {0x87, 0x96, 0xe0};          // $DC5E

// ---------------------------------------------------------------- helpers

static uint8_t roomIndex(const Game &g) { return firstRoom(g.level) + g.screen; } // $F630
static bool minerScreen(const Game &g) { return g.screen == hero_data::minerScreen(g.level); }
static uint8_t objType(const Game &g, int band) {
  return b1((band ? ROOM_F8 : ROOM_F7) + roomIndex(g)) & 3;
}

// 6502 decimal mode add of two BCD bytes
static uint8_t bcdAdd(uint8_t a, uint8_t b, uint8_t &carry) {
  uint8_t lo = (a & 0xf) + (b & 0xf) + carry;
  if (lo > 9) { lo += 6; }
  uint8_t hi = (a >> 4) + (b >> 4) + (lo > 0xf);
  if (hi > 9) { hi += 6; }
  carry = hi > 0xf;
  return (uint8_t)(hi << 4) | (lo & 0xf);
}

enum Digits { HUNDREDS = 1, UNITS = 2 };

// $DBB2/$DBBA: BCD points at the hundreds or units byte, extra life every
// 20,000, the game stops at 1,000,000
static void addScore(Game &g, uint8_t bcd, Digits at) {
  if (g.demo) { return; }
  const uint8_t old = g.score[0];
  uint8_t carry = bcd >= 0x99;
  for (int i = at; i >= 0; i--) {
    g.score[i] = bcdAdd(g.score[i], bcd, carry);
    bcd = 0;
  }
  if (carry) {
    g.over = g.demo = 0xff;
    memset(g.score, 0xbb, sizeof(g.score));
  }
  if (((old ^ g.score[0]) & 0x12) && g.lives != MAX_LIVES) { g.lives++; }
}

// $DE6B
static void die(Game &g, uint8_t band) {
  g.respawn_band = band;
  g.respawn = 0xff;
}

// $D9F1: level (re)start
static void startLevel(Game &g) {
  g.hero.x = START_X;
  g.screen = 0;
  g.prev_screen = 0;
  g.furthest = 0;
  g.hero.y = START_Y;
  g.wall = START_WALL;
  for (int i = 0; i < 3; i++) {
    g.obj_state[i] = GONE;
    g.obj_x[i] = NONE;
    g.obj2_x[i] = NONE;
  }
  g.power = 0;
  g.hero.standing = 0;
  g.respawn = START_RESPAWN;
  g.respawn_band = 1;
  g.power_full = 0;
  g.hero.facing = 0;
  g.wear = 0;
  g.dynamite = START_DYNAMITE;
  g.hue = b0(0xdd13 + (g.level & 3));
}

// the original's RAM map
uint8_t *ram(Game &g, uint8_t a) {
  VideoState &v = g.view;
  switch (a) {
  case 0x80: return &g.game;
  case 0x81: return &g.frame;
  case 0x82: return &g.random;
  case 0x83: return &g.select_delay;
  case 0x84: return &g.joy;
  case 0x9b: return &g.hero.x;
  case 0x9c: return &g.screen;
  case 0x9d: return &g.prev_screen;
  case 0x9e: return &g.furthest;
  case 0x9f: return &g.hero.y;
  case 0xa0: return &g.wall;
  case 0xa1: case 0xa2: case 0xa3: return &g.obj_state[a - 0xa1];
  case 0xa4: return &g.hue;
  case 0xa5: case 0xa6: case 0xa7: return &g.obj_x[a - 0xa5];
  case 0xa8: case 0xa9: case 0xaa: return &g.obj2_x[a - 0xa8];
  case 0xab: return &g.power;
  case 0xac: return &g.hero.standing;
  case 0xad: return &g.respawn;
  case 0xae: return &g.respawn_band;
  case 0xaf: return &g.power_full;
  case 0xb0: return &g.hero.facing;
  case 0xb1: return &g.wear;
  case 0xb2: return &g.dynamite;
  case 0xb3: return &g.lives;
  case 0xb4: return &g.idle;
  case 0xb5: return &g.timer;
  case 0xb6: return &g.demo;
  case 0xb7: case 0xb8: case 0xb9: return &g.score[a - 0xb7];
  case 0xba: return &g.over;
  case 0xbd: return &g.raft_ride;
  case 0xbf: return &v.band[BAND_BOTTOM].pf1;
  case 0xc0: return &v.band[BAND_MID].pf1;
  case 0xc1: return &v.band[BAND_TOP].pf1;
  case 0xc2: return &v.band[BAND_BOTTOM].pf2;
  case 0xc3: return &v.band[BAND_MID].pf2;
  case 0xc4: return &v.band[BAND_TOP].pf2;
  case 0xc5: case 0xc6: case 0xc7: return &v.band_color[2 - (a - 0xc5)];
  case 0xcb: case 0xcc: case 0xcd: return &v.obj[a - 0xcb].gfx;
  case 0xce: case 0xcf: case 0xd0: return &v.obj[a - 0xce].color;
  case 0xd1: case 0xd2: case 0xd3: return &g.latch.hero_obj[a - 0xd1];
  case 0xd4: case 0xd5: case 0xd6: return &g.latch.laser_obj[a - 0xd4];
  case 0xd8: case 0xd9: case 0xda: return &v.obj2[a - 0xd8].gfx;
  case 0xdb: case 0xdc: case 0xdd: return &v.obj2[a - 0xdb].color;
  case 0xde: case 0xdf: case 0xe0: return &g.fuse[a - 0xde];
  case 0xe1: return &g.flash;
  case 0xe2: case 0xe3: case 0xe4: return &g.hero_pf[a - 0xe2];
  case 0xe6: return &g.hero.blocked;
  case 0xe7: return &g.hero.prop;
  case 0xe8: return &g.hero.walk;
  case 0xe9: return &g.hero.thrust;
  case 0xea: case 0xeb: case 0xec: return &g.osc[a - 0xea];
  case 0xed: case 0xee: case 0xef: case 0xf0: case 0xf1: return &g.saved[a - 0xed];
  case 0xf2: return &g.rescued;
  case 0xf3: return &g.laser;
  case 0xf4: return &g.laser_hit;
  case 0xf5: return &g.level;
  case 0xf6: return &g.anim;
  case 0xf7: case 0xf8: case 0xf9: return &g.hit[a - 0xf7];
  case 0xfa: return &g.blast;
  case 0xfb: return &v.wave;
  case 0xfc: return &g.hero.turn_delay;
  case 0xfd: return &g.wall_phase;
  default: return nullptr;
  }
}

// ------------------------------------------------------ screen change

// $F63A: entering g.screen from screen `from`
static void enterScreen(Game &g, uint8_t from) {
  for (int i = 0; i < 3; i++) {
    if (g.hit[i]) {
      g.obj_state[i] = GONE;
      g.hit[i] = 0;
    }
    g.obj2_x[i] = NONE;
  }
  g.wear = 0;
  g.blast = 0;
  g.osc[0] = g.osc[1] = 0;
  g.anim = 0;
  g.wall_phase = 0x50;
  g.osc[2] = g.hero.facing < FACE_LEFT ? 0x80 : 0x60;

  // $A0-$A4: the previous screen gets its state back, others start over
  uint8_t *state[5] = {&g.wall, &g.obj_state[0], &g.obj_state[1], &g.obj_state[2], &g.hue};
  const bool back = g.screen == g.prev_screen;
  g.prev_screen = from;
  if (back) {
    for (int i = 0; i < 5; i++) {
      const uint8_t t = *state[i];
      *state[i] = g.saved[i];
      g.saved[i] = t;
    }
    return;
  }
  for (int i = 0; i < 5; i++) { g.saved[i] = *state[i]; }
  g.hue = b1(LEVEL_HUE + (g.level & 3));

  const uint8_t fe = b1(ROOM_FE + roomIndex(g)) & 0xfc;
  const uint8_t a0 = fe ? fe | 1 : 0;
  uint8_t alive = 0;
  if (g.furthest == g.screen) { // first visit
    g.wall = a0;
  } else {                      // older screen: edge walls only, no critters
    g.wall = (a0 == EDGE_WALL_L || a0 == EDGE_WALL_R) ? a0 : 0;
    alive = GONE;
  }
  for (int i = 0; i < 3; i++) { g.obj_state[i] = alive; }
}

// ------------------------------------------------- overscan, second half

// $D7B6: death sequence, respawn glide and wait
static void respawn(Game &g) {
  Hero &h = g.hero;
  if (g.respawn > DYING) {
    // drowning: sink every 8 frames
    if (g.screen >= 10 && h.y < WATER_Y && (g.frame & 7) == 0 && h.y) { h.y--; }
    g.respawn--;
    return;
  }
  if (g.respawn == DYING) {
    h.y = RESPAWN_TOP_Y;
    g.obj_state[g.respawn_band] = GONE; // the killer goes too
    h.standing = 0;
    if (!g.demo && --g.lives == 0) { g.over = g.demo = 0xff; }
  }
  if (h.y != RESPAWN_Y[g.respawn_band]) {
    h.y--;
    if (--g.respawn) { return; }
  }
  if (g.joy != JOY_NONE && g.power_full) {
    g.respawn = 0;
    h.thrust = THRUST_HOVER;
  }
}

// $D817: landing, ceiling, side push from the band latches
static void collide(Game &g) {
  Hero &h = g.hero;
  h.blocked = 0;
  uint8_t standing = 0;
  bool pushed = false;
  for (int i = 2; i >= 0; i--) {
    if (g.hero_pf[i] & 0x80) {
      if (h.y == FLOOR_Y[i] || h.y + 1 == FLOOR_Y[i]) {
        h.y = FLOOR_Y[i];
        h.blocked = 0x40;
        standing = 0x40;
        if (!(g.joy & JOY_RIGHT) || !(g.joy & JOY_LEFT)) { // walking
          if (!(h.standing & 0x80)) { h.walk = 2; }
          standing = 0x80;
        }
        continue;
      }
      if (h.y == CEILING_Y[i] || h.y == CEILING_Y[i] + 1) {
        h.y = CEILING_Y[i];
        h.blocked = 0x80;
        continue;
      }
    } else if (!(g.hero_pf[i] & 0x40) || !g.wall) {
      continue;
    }
    if (!pushed) { // $D872: once per frame, against the facing
      pushed = true;
      h.x += h.facing ? 1 : -1;
    }
  }
  h.standing = standing;
}

// $D889: laser and touch hits per band, lava
static void objectHits(Game &g) {
  for (int i = 2; i >= 0; i--) {
    if (!g.hit[i] && (g.latch.laser_obj[i] & 0x80)) {
      if (i == 2) { // the lamp
        g.hue = DARK;
      } else {
        addScore(g, 0x50, UNITS);
        g.hit[i] = HIT_TIME;
      }
    }
    if ((g.latch.hero_obj[i] & 0x80) && !g.hit[i]) {
      if (i == 2) {
        g.hue = DARK;
      } else {
        die(g, i);
      }
    }
    if ((g.hero_pf[i] & 0xc0) && (g.view.band_color[2 - i] & 0xf0) == 0x40) { // lava
      int band = i;
      if (g.hero.y >= 0x63) { band++; }
      if (g.hero.y < 0x25) { band--; }
      die(g, band);
    }
  }
}

// $D8E3: joystick exactly down while standing on a 4 px step
static void placeDynamite(Game &g) {
  const Hero &h = g.hero;
  if (!h.standing || !g.dynamite) { return; }
  const int band = h.y >= 0x62 ? 2 : 1;
  if (g.obj2_x[band] != NONE || g.fuse[band] || g.joy != JOY_ONLY_DOWN) { return; }
  if ((h.x & 3) || h.x < 0x0d) { return; }
  g.obj2_x[band] = h.x;
  g.fuse[band] = FUSE;
}

// $D912: on the miner screen within 15 px of the miner
static void rescue(Game &g) {
  if (g.respawn || !minerScreen(g)) { return; }
  if ((uint8_t)(g.hero.x - (g.obj2_x[1] - 7)) >= 0x0f) { return; }
  addScore(g, 0x10, HUNDREDS);
  g.rescued = 0xff;
}

// $D932: 1 unit per 64 frames once the joystick moved, empty = level restart
static void drainPower(Game &g) {
  if (!(g.power_full & 0x80) || (g.frame & 0x3f) || --g.power) { return; }
  startLevel(g);
  g.respawn = 0xff;
}

// $D949: the dynamite's blast at fuse $1F
static void explode(Game &g) {
  int i = 2;
  while (i >= 0 && g.fuse[i] != FUSE_BANG) { i--; }
  if (i < 0) { return; }
  g.dynamite--;
  const uint8_t d = g.obj2_x[i];

  const uint8_t lo = g.obj_x[i] - 0x13, hi = g.obj_x[i] + 0x10;
  if (g.obj_state[i] != GONE && d > lo && d <= hi && !g.hit[i] && i != 2) {
    g.hit[i] = HIT_TIME;
    addScore(g, 0x50, UNITS);
  }
  if (g.wall < EDGE_WALL_R && d > (uint8_t)(g.wall - 0x0f) && d <= (uint8_t)(g.wall + 0x0c)) {
    g.wall = 0;
    g.blast = 0x75;
    addScore(g, 0x75, UNITS);
  }

  Hero &h = g.hero;
  if (h.y < BLAST_Y_LO[i] || h.y >= BLAST_Y_HI[i] || d >= NONE) { return; }
  if ((uint8_t)(h.x - (uint8_t)(d - 8)) >= 0x11) { return; }
  h.x += h.facing ? 1 : -1;
  die(g, i);
}

// $D9C7: power fill during the first respawn of a level
static void fillPower(Game &g) {
  if ((g.over | g.power_full) & 0x80 || g.respawn >= POWER_FILL_BELOW) { return; }
  if (++g.power > POWER_MAX) {
    g.power = POWER_MAX;
    g.power_full = 0xff;
  }
}

// $D735: rescued: count the power and dynamite bonus, then the next level
static void levelDone(Game &g) {
  if (g.hero.standing) { g.hero.standing = 0x40; }
  if (g.power) {
    if (g.frame & (g.level >= 6 ? 3 : 2)) { return; }
    addScore(g, b0(0xd03d + g.level), UNITS);
    addScore(g, b0(0xd051 + g.level), HUNDREDS);
    if (--g.power) { return; }
  }
  if (g.fuse[1]) { return; }
  if (g.dynamite) {
    g.dynamite--;
    addScore(g, 0x50, UNITS);
    g.fuse[1] = 0x10;
    return;
  }

  g.rescued = 0;
  if (!g.demo && (g.game & 0x7f) == 4) { // game 5: random level
    const uint8_t r = g.random & 0x1f;
    g.level = r < 20 ? r : r - 12;
  } else {
    g.level++;
    if (g.demo) { g.level &= 3; }
    if (g.level > 19) {
      g.game |= 0x80;
      g.level = LEVEL_LOOP;
    }
  }
  startLevel(g);
}

static void overscanB(Game &g, const Input &in) {
  if (in.start || (g.idle & 0x80) || g.over) { return; }
  if (g.rescued) {
    levelDone(g);
    return;
  }
  if (g.respawn) {
    respawn(g);
  } else {
    if (!g.flash) { collide(g); }
    objectHits(g);
    placeDynamite(g);
    rescue(g);
    drainPower(g);
  }
  explode(g);
  fillPower(g);
}

// ------------------------------------------------------- frame logic

// $F106: standing on the raft moves the hero with it
static void rideRaft(Game &g) {
  g.raft_ride = 0;
  if (g.view.obj2[0].gfx != RAFT_GFX || g.respawn || g.screen < DEEP) { return; }
  if (!(g.latch.bottom_pp & 0x80)) { return; }
  const uint8_t raft = (g.osc[2] & 0x7f) + 0x18;
  if ((uint8_t)(g.hero.x - raft) >= 9) { return; }
  g.hero.blocked = g.hero.standing = 0x40;
  g.hero.facing = (g.osc[2] & 0x80) ? 0 : FACE_LEFT;
  g.raft_ride = 0xff;
  g.hero.x = raft + 4;
}

// $F14B: the tentacle pulls the hero towards it
static void tentaclePull(Game &g) {
  const uint8_t gfx = g.view.obj2[0].gfx;
  if (gfx == RAFT_GFX || gfx == BLANK_GFX2 || (g.latch.hero_obj[0] & 0x80) || g.respawn) { return; }
  if (!(g.latch.bottom_pp & 0x80) || g.screen < DEEP) { return; }
  const uint8_t t = g.obj2_x[0];
  g.joy = g.hero.x == t ? JOY_NONE : (g.hero.x < t ? 0x70 : 0xb0);
}

// $F184
static void moveVertical(Game &g) {
  Hero &h = g.hero;
  if (!(g.joy & JOY_UP)) {
    if (h.thrust < THRUST_MAX) { h.thrust++; }
  } else if (h.thrust > 0) {
    h.thrust--;
  }
  uint8_t i = h.thrust >> 3;
  if (g.screen >= DEEP && i == 0) { i = 1; }
  if (i >= 4) {
    if (h.blocked & 0x80) { return; }
    const int y = h.y + SPEED[i];
    if (y < TOP_EXIT_Y) {
      h.y = y;
      return;
    }
    if (g.screen == 0) {
      h.y = TOP_EXIT_Y;
      return;
    }
    g.screen--;
    h.y = ENTER_BOTTOM_Y;
    enterScreen(g, g.screen + 1);
    return;
  }

  if (h.blocked & 0x40) { return; }
  int y = h.y - SPEED[i];
  if (y < WATER_Y && g.screen >= 10) {
    die(g, 0);
    y = WATER_Y - 1;
  }
  if (y >= 0) {
    h.y = y;
    return;
  }
  if (g.furthest == g.screen) { g.furthest++; }
  g.screen++;
  h.y = ENTER_TOP_Y;
  enterScreen(g, g.screen - 1);
}

// $F20E
static void moveHorizontal(Game &g) {
  Hero &h = g.hero;
  if (g.raft_ride || g.respawn) { return; }
  if ((h.x & 3) == 0) {
    uint8_t facing;
    if (!(g.joy & JOY_RIGHT)) {
      facing = 0;
    } else if (!(g.joy & JOY_LEFT)) {
      facing = FACE_LEFT;
    } else {
      return;
    }
    if (facing != h.facing) {
      g.laser = 0xfd;
      if (!h.standing) { h.turn_delay = TURN_DELAY; }
    }
    h.facing = facing;
  }
  if (h.turn_delay) {
    h.turn_delay--;
    return;
  }
  int dir;
  if (h.facing) {
    if (--h.x >= LEFT_EXIT_X) { return; }
    dir = 0;
  } else {
    if (++h.x < RIGHT_EXIT_X) { return; }
    dir = 1;
  }
  // $F25E: F9 bit 0 is the side the level came from
  const uint8_t from = g.screen;
  if ((b1(ROOM_F9 + roomIndex(g)) & 1) == dir) {
    g.screen--;
  } else {
    if (g.furthest == g.screen) { g.furthest++; }
    g.screen++;
  }
  h.x = dir ? 0x0f : 0x93; // $FFAC
  enterScreen(g, from);
}

// $F29D: playfield and band colours
static void playfield(Game &g, VideoState &v) {
  const Room room = decode(g.level, g.screen);
  memcpy(v.band, room.band, sizeof(v.band));
  const uint8_t r = roomIndex(g);

  // $F334: the middle band's PF0 also opens for an edge wall seen now
  if (g.screen < 10 && !minerScreen(g)) {
    v.band[BAND_MID].pf0 = (g.wall == EDGE_WALL_L || g.wall == EDGE_WALL_R) ? 0x00 : 0xff;
  }
  // $F355: moving wall, levels 14+
  if (v.band[BAND_MID].pf2 == 0x7f && v.band[BAND_MID].pf1 == 0xff && g.level >= 13) {
    v.band[BAND_MID].pf2 = b1(0xffb8 + ((g.wall_phase >> 3) & 7));
  }

  // $F284: lamp out: black walls, dim grey while dynamite burns in band 1/2
  for (int i = 0; i < 3; i++) {
    uint8_t c = b1(BAND_LUM + i) | g.hue;
    if (g.hue & DARK) { c = (g.fuse[2] | g.fuse[1]) ? 2 : 0; }
    v.band_color[2 - i] = c;
  }
  if (b1(ROOM_FE + r) & 2) { v.band_color[BAND_MID] = b1(LAVA_COLOR + ((g.frame >> 3) & 1)); }

  v.hue = g.hue;
  v.ball = g.wall;
  v.ball_w = g.wear >= 0x80 ? 4 : 8; // $FF3E
  v.water = g.screen >= 10 || minerScreen(g);
  v.water_color = b0(WATER_COLOR + (g.level & 3));
  v.wave = v.water ? (g.frame >> 2) & 7 : 0; // $F60C
  v.background = g.flash;
}

// $F2A0 positions, $F374 sprite pointers, $F3AF motion, $F408 hit flash
static void critters(Game &g, VideoState &v) {
  const uint8_t r = roomIndex(g);
  const uint8_t base[3] = {b1(ROOM_F7 + r), b1(ROOM_F8 + r), b1(ROOM_F9 + r)};
  for (int i = 0; i < 2; i++) { g.obj_x[i] = base[i] == NONE ? NONE : base[i] & 0xfc; }
  g.obj_x[2] = (base[2] & 0xfc) | 1;
  if (g.obj_x[2] == 0x91) { g.obj_x[2] = NONE; }

  // per type: graphics frame by $F6, colours fixed
  const uint8_t f1 = (g.anim >> 3) & 1, f2 = (g.anim >> 2) & 3;
  const uint8_t gfx[4] = {b1(0xff8b + f1), b1(0xff8d + f1), b1(0xff8f + f2), b1(0xff9b + f1)};
  static const uint8_t COLOR[4] = {0x4d, 0x5a, 0x33, 0x17};
  const uint8_t dy = g.osc[0] & 0x7f, dx = g.osc[1] & 0x7f;

  for (int i = 2; i >= 0; i--) {
    Sprite &s = v.obj[i];
    if (i == 2) { // the lamp
      s.gfx = 0x64;
      s.color = 0x8e;
    } else {
      const uint8_t type = objType(g, i);
      if (g.obj_state[i] != GONE) {
        if (type == 3) {
          g.obj_x[i] += g.hit[i] ? 9 : dy; // sideways
        } else if (type) {
          g.obj_state[i] = dy;            // up and down
          if (type == 2) { g.obj_x[i] += dx; }
        }
      }
      s.gfx = gfx[type];
      s.color = COLOR[type];
    }
    if (g.hit[i]) {
      if (--g.hit[i] == 0) { g.obj_state[i] = GONE; }
      s.gfx = HIT_GFX;
      s.color = 0x40;
    }
    if (g.obj_x[i] >= NONE) { s.gfx = 0x8a; }
    s.x = g.obj_x[i];
    s.offset = g.obj_state[i];
  }

  // $F42F: in the dark only hit critters keep their colour, unless dynamite burns
  if (!(g.hue & DARK)) { return; }
  for (int i = 2; i >= 0; i--) {
    if (g.fuse[i]) { return; }
    if (v.obj[i].gfx != HIT_GFX) { v.obj[i].color = 0x9b; }
  }
}

// $F447: dynamite and explosion
static void dynamite(Game &g, VideoState &v) {
  for (int i = 2; i >= 0; i--) {
    Sprite &s = v.obj2[i];
    s.gfx = b1(0xff93 + g.hero.prop);
    s.color = 0x68;
    if (g.fuse[i]) {
      const uint8_t fuse = --g.fuse[i];
      if (!fuse) {
        g.blast = 0;
        g.obj2_x[i] = NONE;
      } else if (fuse < 0x20) {
        const uint8_t k = (fuse < 0x13 && g.blast) ? 4 : (fuse >> 1) & 3;
        s.gfx = b1(0xff96 + k);
        s.color = 0x40;
        if (!g.rescued) { g.flash = b1(0xffa5 + k); }
      }
    }
    if (g.obj2_x[i] == NONE) { s.gfx = BLANK_GFX2; }
  }
}

// $F499 raft (levels 10+), $F4C0 tentacle (levels 17+) in deep water rooms
static void waterObjects(Game &g, VideoState &v) {
  Sprite &s = v.obj2[0];
  const Band &b = v.band[BAND_BOTTOM];
  if (g.screen >= DEEP && g.level >= 9 && !b.pf2 && b.pf1 == 0xc0) {
    s.gfx = RAFT_GFX;
    s.color = 0x05;
    g.obj2_x[0] = (g.osc[2] & 0x7f) + 0x1c;
  }
  if (g.level < 16 || g.screen < DEEP || b.pf2 || b.pf1 != 0xe0 || s.gfx == RAFT_GFX) { return; }

  uint8_t &t = g.obj2_x[0];
  if (t == NONE) {
    t = (g.hero.facing & FACE_LEFT) ? 0x78 : 0x1c;
  } else {
    if (t == 0x92) { return; }
    if (g.respawn == START_RESPAWN && !g.respawn_band) {
      t = 0x92; // the hero died in the water: gone
      return;
    }
    // follows the hero, every 4th frame (2nd from level 19)
    if (!(g.frame & (g.level >= 18 ? 1 : 3)) && t != g.hero.x) {
      if (t < g.hero.x) {
        t = t + 1 >= 0x78 ? 0x78 : t + 1;
      } else {
        t = t - 1 < 0x1c ? 0x1c : t - 1;
      }
    }
  }
  uint8_t k = (g.anim >> 3) & 7;
  if (g.respawn) { k = b1(0xffc0 + (g.respawn >> 5)); }
  s.gfx = b1(0xf6d2 + k);
  s.color = 0xc4;
}

// $F54A: hero graphics
static void heroSprite(const Game &g, VideoState &v) {
  const Hero &h = g.hero;
  v.hero_x = h.x;
  v.hero_y = h.y;
  v.hero_mirror = h.facing != 0;
  v.hero_page = 0xde;
  if (g.respawn >= DYING) {
    v.hero_page = 0xda;
    v.hero_frame = 0xe2;
  } else if (g.flash) {
    v.hero_frame = 0xe6;
  } else if (h.standing & 0x80) {
    v.hero_page = 0xdc;
    v.hero_frame = b1(0xff9d + h.walk);
  } else if (h.standing) {
    v.hero_frame = 0xcc;
  } else {
    v.hero_frame = b1(0xffa2 + h.prop);
  }
}

// $F59B: the miner, mirrored on the left
static void miner(Game &g, VideoState &v) {
  v.p0_mirror = 0;
  if (!minerScreen(g)) { return; }
  v.obj2[1].gfx = 0x71;
  v.obj2[1].color = 0x26;
  g.obj2_x[1] = 0x80;
  if ((b1(ROOM_FE + b1(0xff14 + g.level)) & 0xfc) == 8) {
    for (int i = 0; i < 2; i++) {
      if (v.obj[i].gfx == HIT_GFX) { v.obj[i].gfx = 0x6d; }
    }
    v.p0_mirror = 1;
    g.obj2_x[1] = 0x18;
  }
  if (g.rescued) { v.obj2[1].gfx = 0x7e; }
}

// $D079: score, or "LEVEL: n" until the power is full
static void hudText(const Game &g, VideoState &v) {
  uint8_t b[3] = {g.score[0], g.score[1], g.score[2]};
  bool level;
  if (g.timer & 0x80) {
    level = false;
  } else if (g.over) { // game over: the score blinks with the level
    level = !g.lives && g.score[0] != 0xaa && (g.demo & 0x1f) >= 0x14;
  } else {
    level = !g.power_full;
  }
  if ((g.game & 0x7f) == 4) { level = false; }
  if (level) {
    b[0] = 0xcd;
    b[1] = 0xef;
    b[2] = b0(0xd02a + g.level);
  }
  for (int i = 0; i < 3; i++) {
    v.text[2 * i] = (b[i] & 0xf0) >> 1;
    v.text[2 * i + 1] = (b[i] & 0x0f) << 3;
  }
  for (int i = 0; i < 5 && !v.text[i]; i++) { v.text[i] = 0x50; } // leading blanks
  if (level && (g.game & 0x80)) {
    v.text[4] = 0xd8;
    v.text[5] = 0xe0;
  }
  v.text_color = v.text[0] == 0x60 ? 0x1c : 0x0c;
  const uint8_t t = g.demo & 0x1f; // $D47F
  v.copyright = t >= 0x14 ? 7 : (t < 0x0c ? 0 : t - 0x0c);
  v.power = g.power;
  v.lives = g.lives;
  v.dynamite = g.dynamite;
}

static void display(Game &g);

// $F093: SELECT shows the game number as the score and starts the attract timer
static void selectScreen(Game &g) {
  uint8_t n = g.game & 0x7f;
  if (n >= 5) {
    g.game &= 0x80;
    n = 0;
  }
  g.idle = g.timer = n;
  g.score[0] = g.score[1] = 0xaa; // blank digits
  g.score[2] = (n | 0xa0) + 1;
  g.demo = 0x1f;
  g.select_delay = 0x1e;
}

// $F007: clears RAM from `from` up, then game $80 (level 1 for the demo)
static void restart(Game &g, uint8_t from) {
  for (int a = from; a <= 0xff; a++) {
    uint8_t *p = ram(g, a);
    if (p) { *p = 0; }
  }
  uint8_t game = 0;
  if (!g.demo) {
    g.game &= 0x7f;
    game = g.game;
  }
  g.level = b1(0xff6d + game);
  startLevel(g);
  g.lives = START_LIVES;
  if (g.random) { return; }
  g.random = 1; // power on
  g.over = 0xff;
  selectScreen(g);
}

void powerOn(Game &g) {
  memset(&g, 0, sizeof(g));
  g.laser_pos = 1;
  restart(g, 0x80);
}

// $F042: joystick and console switches; the attract demo ignores the
// joystick and restarts every 2048 frames
static void consoleInput(Game &g, const Input &in) {
  uint8_t swcha = 0xff;
  if (in.right) { swcha &= ~JOY_RIGHT; }
  if (in.left) { swcha &= ~JOY_LEFT; }
  if (in.down) { swcha &= ~JOY_DOWN; }
  if (in.up) { swcha &= ~JOY_UP; }
  if (!(g.frame & 7) && g.demo) {
    swcha = 0xff;
    if (!--g.demo) {
      g.demo--;
      if (!(g.timer & 0x80)) {
        g.timer |= 0x80;
        restart(g, 0xba);
        return;
      }
    }
  }

  g.joy = swcha & 0xf0;
  if (swcha != 0xff) { g.idle = 0; }
  if (in.start) {
    g.over = 0;
    restart(g, 0xb4);
    return;
  }
  if (!in.select) {
    g.select_delay = 0;
    return;
  }
  g.over = g.respawn = 0xff;
  if (g.select_delay) {
    g.select_delay--;
    return;
  }
  g.game++;
  selectScreen(g);
}

// $F0DB: the demo's joystick per room, dynamite at walls, away from its fuse
static uint8_t demoJoy(uint16_t table, uint8_t room) {
  const uint16_t a = table + room;
  return a <= 0xffeb ? b1(a) : JOY_NONE; // past the ROM: open bus in the original
}

static void demoInput(Game &g) {
  const uint8_t room = roomIndex(g);
  uint8_t joy = demoJoy(0xff42, room);
  if (g.hero.standing) {
    if (g.fuse[1]) {
      if (g.fuse[1] >= 0x2c) { joy = demoJoy(0xff56, room); }
    } else {
      const uint8_t a = g.wall - 0x0f;
      if (a < g.hero.x && (uint8_t)(a + 0x17) >= g.hero.x) { joy = JOY_ONLY_DOWN; }
    }
  }
  g.joy = joy;
}

// bank 1, $F042-$F62D
static void frameLogic(Game &g, const Input &in) {
  consoleInput(g, in);
  if (!(g.respawn | g.rescued | g.hit[0] | g.hit[1])) { g.anim++; }
  g.random = (uint8_t)(g.random << 1) | (((g.random << 3) ^ g.random) >> 7 & 1);
  if (g.demo) {
    if (g.idle & 0x80) { // blank screen saver
      display(g);
      return;
    }
    demoInput(g);
  }

  rideRaft(g);
  tentaclePull(g);
  if (!(g.flash | g.over | g.rescued | g.respawn)) {
    moveVertical(g);
    moveHorizontal(g);
  }
  display(g);
}

// $F282-$F62D, $D079: display registers
static void display(Game &g) {
  VideoState &v = g.view;
  playfield(g, v);
  critters(g, v);
  dynamite(g, v);
  waterObjects(g, v);
  heroSprite(g, v);
  miner(g, v);
  for (int i = 0; i < 3; i++) { v.obj2[i].x = g.obj2_x[i]; }
  v.laser_x = g.laser_pos;
  hudText(g, v);
  v.blank = g.idle & 0x80; // $D104
}

void viewRoom(Game &g, uint8_t level, uint8_t screen) {
  powerOn(g);
  g.over = g.demo = 0;
  g.level = level;
  g.screen = g.prev_screen = g.furthest = screen;
  const Room room = decode(level, screen);
  g.wall = room.ball_x == NO_BALL ? 0 : room.ball_x + 1;
  g.hue = b1(LEVEL_HUE + (level & 3));
  memset(g.obj_state, 0, sizeof(g.obj_state));
  g.respawn = 0;
  g.power = POWER_MAX;
  g.power_full = 0xff;
  display(g);
}

// ------------------------------------------------------------ kernel

// P1 vs playfield/ball latches $E2-$E4 of the hero's rows: row k (0 top)
// counts for band i when y + 24 - k is in [21 + 39 i, 60 + 39 i) (HERO.md
// "Remake movement model"), the other latches from the kernel's lines
static void kernel(Game &g) {
  const VideoState &v = g.view;
  uint64_t c[3];
  for (int i = 0; i < 3; i++) {
    g.hero_pf[i] = 0;
    c[i] = cells(v.band[2 - i]);
  }
  const int ball = (g.wall ? g.wall : 1) - 1;
  for (int k = 0; k < hero_video::HERO_ROWS; k++) {
    const int u = g.hero.y + HERO_HEIGHT - k - BAND_BOTTOM_Y;
    if (u < 0 || u >= 3 * BAND_H || !(b0(0xdd8d - k) & 1)) { continue; }
    const int i = u / BAND_H;
    const uint8_t bits = b0((v.hero_page << 8 | v.hero_frame) + 0x19 - k);
    for (int b = 0; b < 8; b++) {
      if (!(bits & (v.hero_mirror ? 1 << b : 0x80 >> b))) { continue; }
      const int px = g.hero.x + b;
      if (px < 160 && ((c[i] >> (px / 4)) & 1)) { g.hero_pf[i] |= 0x80; }
      if (px >= ball && px < ball + v.ball_w) { g.hero_pf[i] |= 0x40; }
    }
  }
  g.hero.blocked = 0xff; // $E6 ends as the kernel's band counter
  hero_video::collide(g.view, g.latch);
  g.laser_hit |= g.latch.laser_wall;
}

// ------------------------------------------------- overscan, first half

// $D51A: critter motion, bouncing between 0 and $DC5E[x] & $7F
static void oscillate(Game &g) {
  int i = 1;
  if (g.hero.standing == 0x40 && g.hero.y == 0x17) { i = 2; } // riding the raft
  for (; i >= 0; i--) {
    if (i < 2) {
      if (g.respawn | g.rescued | g.hit[0] | g.hit[1]) { continue; }
      const uint8_t mask = g.level < 9 ? 3 : (g.level < 17 ? 1 : 0);
      if (g.anim & mask) { continue; }
    }
    uint8_t &o = g.osc[i];
    if (o & 0x80) {
      if (++o > OSC_MAX[i]) { o = OSC_MAX[i] & 0x7f; }
    } else if (--o & 0x80) {
      o = 0x80;
    }
  }
}

// $D564: propeller and walk animation
static void animate(Game &g) {
  Hero &h = g.hero;
  if (g.rescued) {
    h.walk = 3;
    h.prop = 0;
    return;
  }
  if (g.frame & 2) { h.prop = h.prop ? h.prop - 1 : 2; }
  if ((g.frame & 3) == 0) { h.walk = h.walk ? h.walk - 1 : 4; }
}

// $D58C: channel 0 laser, death and bonus tones, channel 1 noise for the
// propeller, power fill, fuse and explosion; silent in the demo
static void sound(Game &g, const Input &in) {
  Sound &s = g.sound;
  memset(&s, 0, sizeof(s));
  if (g.demo) { return; }

  s.audv[0] = 5;
  uint8_t c = 0, f = 0;
  if (g.respawn) {
    if (g.respawn >= DYING) {
      s.audv[0] = (g.respawn - 0x60) >> 4;
      c = b0(0xdba0 + ((g.frame >> 1) & 1));
      f = 0x0f;
    }
  } else if (g.rescued) {
    if (g.power) {
      const uint8_t i = (g.frame >> 1) & 3;
      c = b0(0xdba7 + i);
      f = b0(0xdbab + i);
    }
  } else if (in.fire && !(g.laser & 0x80)) {
    const uint8_t i = g.frame & 3;
    c = b0(0xdb9f + i);
    f = b0(0xdba3 + i);
  }
  s.audc[0] = c;
  s.audf[0] = f;

  s.audc[1] = 8; // 9 bit poly noise
  if (g.fuse[1] | g.fuse[2]) {
    const uint8_t fuse = g.fuse[1] ? g.fuse[1] : g.fuse[2];
    if (fuse >= 0x20) { // burning
      s.audf[1] = s.audv[1] = g.random & 3;
    } else {            // explosion
      s.audv[1] = fuse >> 1;
      s.audf[1] = 0x10;
    }
    return;
  }
  if (g.power && !(g.power_full & 0x80)) { // filling
    s.audf[1] = ~(g.power >> 2);
    s.audv[1] = 4;
    return;
  }
  if (g.hero.standing | g.respawn | g.rescued) { return; }
  s.audf[1] = b0(0xdbaf + g.hero.prop); // propeller
  s.audv[1] = g.hero.prop + 1;
}

// $D638: moving wall phase, stopped by a death
static void wallPhase(Game &g) {
  if (g.respawn >= 0xe0) { return; }
  if (g.respawn) { g.wall_phase |= 0x80; }
  if (!(g.wall_phase & 0x80)) {
    g.wall_phase = (g.wall_phase + 1) & 0x3f;
    return;
  }
  const uint8_t p = g.wall_phase & 0xf8;
  if (p != 0x80) { g.wall_phase = p - 1; }
}

// $D65C: the laser while FIRE is held; it wears down a hit wall
static uint8_t laser(Game &g, const Input &in) {
  if (g.over | g.respawn | g.rescued || (!g.demo && !in.fire)) {
    g.laser = 0xfd;
    return 0;
  }
  for (int i = 3; i >= 0; i--) {
    g.laser |= 0x80;
    if ((uint8_t)(NO_LASER_Y[i] - g.hero.y) < 2) {
      g.laser = 0xfd;
      return 0;
    }
  }

  // blocked by a wall: the beam restarts at the gun
  const uint8_t hit = g.laser_hit;
  if (!(hit & 0xc0)) {
    g.laser = (g.laser + 3) & 0x0f;
  } else {
    if (!(hit & 0x80) && g.wall >= 0x0a && g.wall < EDGE_WALL_R && !(g.frame & 1)) {
      if (++g.wear == 0) { g.wall = 0; }
      if (g.wear == 0x80 && g.wall >= g.hero.x) { g.wall += 4; } // narrower, same right edge
    }
    g.laser_hit = 0;
    g.laser = 0;
  }

  const Hero &h = g.hero;
  uint8_t x;
  if (!h.facing) {
    x = 7 + h.x + g.laser;
  } else {
    x = h.x - 5 + (g.wear >= 0x80 ? 4 : 0) - g.laser;
  }
  return x < 0x9f ? x : 0;
}

// $D704, after the $81 wrap: $B4 counts idle time in 2048 frames
static void idleTimer(Game &g) {
  if (g.frame) { return; }
  g.timer = (g.timer + 1) & 0xc7;
  if (g.timer & 7) { return; }
  if (++g.idle == 0) { g.idle = 0x80; }
}

// $D00C positions an object at A, A = 0 as 1
static uint8_t position(uint8_t a) { return a ? a : 1; }

void tick(Game &g, const Input &in) {
  idleTimer(g);
  overscanB(g, in);
  frameLogic(g, in);
  kernel(g);
  oscillate(g);
  animate(g);
  sound(g, in);
  wallPhase(g);
  g.laser_pos = position(laser(g, in));
  g.frame++; // $D700
}

} // namespace hero_game

#endif // ENABLE_HERO
