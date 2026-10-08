#ifdef ENABLE_HERO

#include "hero_game.h"
#include "hero_data.h"

using namespace hero_data;
using namespace hero_room;

namespace hero_game {

static const uint8_t START_X = 0x20;          // $DA04 init block
static const uint8_t START_Y = 0x8b;
static const uint8_t START_RESPAWN = 0x95;
static const uint8_t START_DELAY = 0;         // frames before the glide (MAME)
static const uint8_t START_BALL = 0x3d - 1;   // screen 0 wall
static const uint8_t RESPAWN_DEATH = 0x96;    // $AD >= this: death sequence
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

static const uint8_t SPEED[8] = {2, 1, 0, 0, 0, 0, 1, 2};   // $FFB0
static const uint8_t RESPAWN_Y[4] = {0x22, 0x49, 0x70, 0x1c}; // $DAFC
static const uint8_t FLOOR_Y[3] = {0x3c, 0x63, 0xef};          // $D9E9
static const uint8_t CEILING_Y[3] = {0xef, 0x24, 0x4b};        // $D9EC

// $F66F: from screen `from` to g.screen
static void enterScreen(Game &g, uint8_t from) {
  g.room = decode(g.level, g.screen);
  if (g.screen == g.prev_screen) { // back: swap with the saved state
    const uint8_t b = g.ball_x;
    g.ball_x = g.saved_ball_x;
    g.saved_ball_x = b;
  } else {
    g.saved_ball_x = g.ball_x;
    if (g.screen == g.furthest) {
      g.ball_x = g.room.ball_x; // first visit
    } else {                    // older screen: edge walls only
      const uint8_t b = g.room.ball_x;
      g.ball_x = (b == 8 || b == 152) ? b : NO_BALL;
    }
  }
  g.prev_screen = from;
}

void startLevel(Game &g, uint8_t level) {
  g = Game();
  g.level = level;
  g.screen = 0;
  g.room = decode(level, 0);
  g.ball_x = START_BALL;
  g.hero.x = START_X;
  g.hero.y = START_Y;
  g.start_delay = START_DELAY;
  g.respawn = START_RESPAWN;
  g.respawn_band = 1;
}

// band i (0 bottom .. 2 top) of the room
static const Band &band(const Game &g, int i) { return g.room.band[BANDS - 1 - i]; }

static bool overlapsBand(const Hero &h, int i) {
  const int lo = BAND_BOTTOM_Y + BAND_H * i;
  return h.y - 1 < lo + BAND_H && h.y + HERO_H >= lo;
}

static bool overlapsX(const Hero &h, int x0, int width) { return h.x < x0 + width && h.x + 8 > x0; }

// TIA P1 vs playfield latch of band i, in software
static bool hitsPlayfield(const Game &g, int i) {
  if (!overlapsBand(g.hero, i)) { return false; }
  const uint64_t c = cells(band(g, i));
  for (int px = g.hero.x; px < g.hero.x + 8; px++) {
    if (px < 160 && ((c >> (px / 4)) & 1)) { return true; }
  }
  return false;
}

static bool hitsBall(const Game &g, int i) {
  return g.ball_x != NO_BALL && overlapsBand(g.hero, i) && overlapsX(g.hero, g.ball_x, 8);
}

// $F184
static void moveVertical(Game &g, const Input &in) {
  Hero &h = g.hero;
  if (in.up) {
    if (h.thrust < THRUST_MAX) { h.thrust++; }
  } else if (h.thrust > 0) {
    h.thrust--;
  }
  uint8_t i = h.thrust >> 3;
  if (g.screen >= 11 && i == 0) { i = 1; }
  if (i >= 4) {
    if (h.blocked & 0x80) { return; }
    const int y = h.y + SPEED[i];
    if (y >= TOP_EXIT_Y) {
      if (g.screen == 0) {
        h.y = TOP_EXIT_Y;
        return;
      }
      g.screen--;
      h.y = ENTER_BOTTOM_Y;
      enterScreen(g, g.screen + 1);
      return;
    }
    h.y = y;
  } else {
    if (h.blocked & 0x40) { return; }
    int y = h.y - SPEED[i];
    if (y < WATER_Y && g.screen >= 10) {
      g.dead = 1;
      y = WATER_Y - 1;
    }
    if (y < 0) {
      if (g.furthest == g.screen) { g.furthest++; }
      g.screen++;
      h.y = ENTER_TOP_Y;
      enterScreen(g, g.screen - 1);
      return;
    }
    h.y = y;
  }
}

// $F20E
static void moveHorizontal(Game &g, const Input &in) {
  Hero &h = g.hero;
  if ((h.x & 3) == 0) {
    uint8_t facing;
    if (in.right) {
      facing = 0;
    } else if (in.left) {
      facing = FACE_LEFT;
    } else {
      return;
    }
    if (facing != h.facing && !h.standing) { h.turn_delay = TURN_DELAY; }
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
  const uint8_t r = firstRoom(g.level) + g.screen;
  const uint8_t from = g.screen;
  if ((b1(ROOM_F9 + r) & 1) == dir) {
    g.screen--;
  } else {
    if (g.furthest == g.screen) { g.furthest++; }
    g.screen++;
  }
  h.x = dir ? 0x0f : 0x93; // $FFAC
  enterScreen(g, from);
}

// $D817: landing, ceiling, side push from the band latches
static void collide(Game &g, const Input &in) {
  Hero &h = g.hero;
  h.blocked = 0;
  uint8_t standing = 0;
  bool pushed = false;
  for (int i = 2; i >= 0; i--) {
    if (hitsPlayfield(g, i)) {
      if (h.y == FLOOR_Y[i] || h.y + 1 == FLOOR_Y[i]) {
        h.y = FLOOR_Y[i];
        h.blocked = 0x40;
        standing = (in.left || in.right) ? 0x80 : 0x40;
        continue;
      }
      if (h.y == CEILING_Y[i] || h.y == CEILING_Y[i] + 1) {
        h.y = CEILING_Y[i];
        h.blocked = 0x80;
        continue;
      }
    } else if (!hitsBall(g, i)) {
      continue;
    }
    if (!pushed) { // $D872: once per frame, against the facing
      pushed = true;
      h.x += h.facing ? 1 : -1;
    }
  }
  h.standing = standing;
}

// $D7B6 glide and wait, $D9C7 power fill
static void respawn(Game &g, const Input &in) {
  Hero &h = g.hero;
  if (h.y != RESPAWN_Y[g.respawn_band]) {
    h.y--;
    g.respawn--;
  } else if ((in.up || in.down || in.left || in.right) && g.power_full) {
    g.respawn = 0;
    h.thrust = THRUST_HOVER;
  }
}

static void power(Game &g) {
  if (g.power_full) {
    if ((g.frame & 0x3f) == 0 && g.power && --g.power == 0) { g.dead = 1; } // $D93C
  } else if (g.respawn && g.respawn < POWER_FILL_BELOW) {
    if (++g.power >= POWER_MAX) {
      g.power = POWER_MAX;
      g.power_full = 1;
    }
  }
}

void tick(Game &g, const Input &in) {
  if (g.start_delay) {
    g.start_delay--;
  } else {
    // overscan of the previous frame
    if (g.respawn) {
      respawn(g, g.last_in);
    } else {
      collide(g, g.last_in);
    }
    power(g);
  }
  // this frame
  g.frame++;
  g.last_in = in;
  if (!g.start_delay && !g.respawn) {
    moveVertical(g, in);
    moveHorizontal(g, in);
  }
  // $D56C, frame counter before this frame's increment
  const uint8_t f = g.frame - 1;
  Hero &h = g.hero;
  if (f & 2) { h.prop = h.prop ? h.prop - 1 : 2; }
  if ((f & 3) == 0) { h.walk = h.walk ? h.walk - 1 : 4; }
}

void heroSprite(const Game &g, uint8_t &page, uint8_t &frame) {
  static const uint8_t FLY[3] = {0x81, 0x9a, 0xb3};              // $FFA2
  static const uint8_t WALK[5] = {0xe5, 0xcc, 0xb3, 0x9a, 0x81}; // $FF9D
  const Hero &h = g.hero;
  page = 0xde;
  if (h.standing & 0x80) {
    page = 0xdc;
    frame = WALK[h.walk];
  } else if (h.standing) {
    frame = 0xcc;
  } else {
    frame = FLY[h.prop];
  }
}

} // namespace hero_game

#endif // ENABLE_HERO
