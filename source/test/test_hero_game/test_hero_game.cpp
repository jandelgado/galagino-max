#include <stdio.h>
#include <unity.h>

#define ENABLE_HERO
#include "../../src/machines/hero/hero_data.cpp"
#include "../../src/machines/hero/hero_room.cpp"
#include "../../src/machines/hero/hero_video.cpp"
#include "../../src/machines/hero/hero_game.cpp"
#include "hero_traces.h"

using namespace hero_game;

void setUp(void) {}
void tearDown(void) {}

static const int SWCHA = 128; // record index after RAM $80-$FF
static const int INPT4 = 129;

// Game field of RAM address `a`, mask of the bits that count; nullptr: not
// modelled
static uint8_t *field(Game &g, uint8_t a, uint8_t &mask) {
  hero_video::VideoState &v = g.view;
  mask = 0xff;
  switch (a) {
  case 0x80: return &g.game;
  case 0x81: return &g.frame;
  case 0x82: return &g.random;
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
  case 0xb7: case 0xb8: case 0xb9: return &g.score[a - 0xb7];
  case 0xbf: return &v.band[hero_room::BAND_BOTTOM].pf1;
  case 0xc0: return &v.band[hero_room::BAND_MID].pf1;
  case 0xc1: return &v.band[hero_room::BAND_TOP].pf1;
  case 0xc2: return &v.band[hero_room::BAND_BOTTOM].pf2;
  case 0xc3: return &v.band[hero_room::BAND_MID].pf2;
  case 0xc4: return &v.band[hero_room::BAND_TOP].pf2;
  case 0xc5: case 0xc6: case 0xc7: return &v.band_color[2 - (a - 0xc5)];
  case 0xcb: case 0xcc: case 0xcd: return &v.obj[a - 0xcb].gfx;
  case 0xce: case 0xcf: case 0xd0: return &v.obj[a - 0xce].color;
  case 0xd1: case 0xd2: case 0xd3: mask = 0x80; return &g.latch.hero_obj[a - 0xd1];
  case 0xd4: case 0xd5: case 0xd6: mask = 0x80; return &g.latch.laser_obj[a - 0xd4];
  case 0xd8: case 0xd9: case 0xda: return &v.obj2[a - 0xd8].gfx;
  case 0xdb: case 0xdc: case 0xdd: return &v.obj2[a - 0xdb].color;
  case 0xde: case 0xdf: case 0xe0: return &g.fuse[a - 0xde];
  case 0xe1: return &g.flash;
  case 0xe2: case 0xe3: case 0xe4: mask = 0xc0; return &g.hero_pf[a - 0xe2];
  case 0xe6: return &g.hero.blocked;
  case 0xe7: return &g.hero.prop;
  case 0xe8: return &g.hero.walk;
  case 0xe9: return &g.hero.thrust;
  case 0xea: case 0xeb: case 0xec: return &g.osc[a - 0xea];
  case 0xed: case 0xee: case 0xef: case 0xf0: case 0xf1: return &g.saved[a - 0xed];
  case 0xf2: return &g.rescued;
  case 0xf3: return &g.laser;
  case 0xf4: mask = 0xc0; return &g.laser_hit;
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

static void load(Game &g, const uint8_t *rec) {
  newGame(g, 0);
  for (int a = 0x80; a <= 0xff; a++) {
    uint8_t mask;
    uint8_t *f = field(g, a, mask);
    if (f) { *f = rec[a - 0x80]; }
  }
}

static Input inputOf(const uint8_t *rec) {
  const uint8_t v = rec[SWCHA];
  Input in = {};
  in.up = !(v & 0x10);
  in.down = !(v & 0x20);
  in.left = !(v & 0x40);
  in.right = !(v & 0x80);
  in.fire = !(rec[INPT4] & 0x80);
  return in;
}

// every modelled RAM byte, every frame of the MAME traces (tools/trace_fixture.py)
static void test_traces_match_mame(void) {
  for (const HeroTrace &t : hero_traces) {
    static Game g;
    load(g, t.rec[0]);
    for (int i = 1; i < t.rows; i++) {
      tick(g, inputOf(t.rec[i]));
      for (int a = 0x80; a <= 0xff; a++) {
        uint8_t mask;
        const uint8_t *f = field(g, a, mask);
        if (!f) { continue; }
        char msg[64];
        snprintf(msg, sizeof(msg), "%s frame %d, $%02X", t.name, i, a);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(t.rec[i][a - 0x80] & mask, *f & mask, msg);
      }
    }
  }
}

// a game in the air on level/screen, respawn over
static Game flying(uint8_t level, uint8_t screen, uint8_t x, uint8_t y) {
  Game g;
  viewRoom(g, level, screen);
  g.wall = 0;
  g.hero.x = x;
  g.hero.y = y;
  g.hero.thrust = 0x1f; // hover
  g.hero.blocked = 0;
  return g;
}

// $F25E: F9 bit 0 of the room is the side the level came from
static void test_horizontal_exit(void) {
  // level 9 (index 8) screen 12: deep, horizontal part
  const uint8_t r = hero_data::firstRoom(8) + 12;
  const uint8_t back = hero_data::b1(hero_data::ROOM_F9 + r) & 1; // 0: came from the left
  Game g = flying(8, 12, back ? 0x92 : 0x0c, 0x40); // middle band, open
  g.hero.facing = back ? 0 : 8; // no turn delay
  Input in = {};
  (back ? in.right : in.left) = 1;
  tick(g, in); // one step past the edge
  TEST_ASSERT_EQUAL_UINT8(11, g.screen);
  TEST_ASSERT_EQUAL_HEX8(back ? 0x0f : 0x93, g.hero.x);
}

// $F1E0: below y $16 on screens >= 10 is water
static void test_water_death(void) {
  Game g = flying(8, 10, 0x40, 0x17);
  g.hero.thrust = 0;
  Input in = {};
  tick(g, in);
  TEST_ASSERT_EQUAL_HEX8(0xff, g.respawn);
  TEST_ASSERT_EQUAL_UINT8(0, g.respawn_band);
}

// $F1C0: screen 0 has no screen above
static void test_top_clamp(void) {
  Game g = flying(0, 0, 0x40, 0x89);
  g.hero.thrust = 0x3f;
  Input in = {};
  in.up = 1;
  tick(g, in);
  tick(g, in);
  TEST_ASSERT_EQUAL_UINT8(0, g.screen);
  TEST_ASSERT_EQUAL_HEX8(0x8a, g.hero.y);
}

// $F66F: first visit takes the room's wall, an older screen only edge walls,
// the previous screen gets its saved state back
static void test_wall_state_on_screen_change(void) {
  Game g = flying(1, 0, 0x40, 0x03); // level 2, screen 1 has a wall at x 52
  g.hero.thrust = 0;
  Input in = {};
  tick(g, in);
  tick(g, in);
  TEST_ASSERT_EQUAL_UINT8(1, g.screen);
  TEST_ASSERT_EQUAL_UINT8(53, g.wall);
  g.wall = 0; // blown up as well
  g.hero.y = 0x89;
  g.hero.thrust = 0x3f;
  g.hero.blocked = 0;
  in.up = 1;
  tick(g, in);
  tick(g, in);
  TEST_ASSERT_EQUAL_UINT8(0, g.screen);
  TEST_ASSERT_EQUAL_UINT8(0, g.wall); // saved state of screen 0
}

// $D93C: the last power unit restarts the level with a death
static void test_power_out(void) {
  Game g = flying(0, 1, 0x40, 0x63);
  g.power = 1;
  g.frame = 0x3f;
  Input in = {};
  tick(g, in); // the overscan sees $81 = $40
  tick(g, in);
  TEST_ASSERT_EQUAL_UINT8(0, g.screen);
  TEST_ASSERT_EQUAL_HEX8(0xff, g.respawn);
  TEST_ASSERT_EQUAL_UINT8(0, g.power_full);
}

// $D7B6 jumps past the drain ($D932) while $AD != 0: waiting at the respawn
// point keeps the power full
static void test_no_drain_while_waiting(void) {
  Game g;
  newGame(g, 0);
  Input in = {};
  for (int i = 0; i < 400; i++) { tick(g, in); }
  TEST_ASSERT_NOT_EQUAL(0, g.respawn);
  TEST_ASSERT_EQUAL_UINT8(0x51, g.power);
}

// $D7DC: the death sequence costs a life and removes the killer
static void test_death_sequence(void) {
  Game g = flying(0, 1, 0x40, 0x63);
  g.lives = 2;
  g.respawn = 0xff;
  g.respawn_band = 1;
  Input in = {};
  for (int i = 0; i < 0xff - 0x96 + 1; i++) { tick(g, in); }
  TEST_ASSERT_EQUAL_UINT8(1, g.lives);
  TEST_ASSERT_EQUAL_HEX8(GONE, g.obj_state[1]);
  TEST_ASSERT_EQUAL_HEX8(0x8b, g.hero.y); // glides down from $8C
}

// $DBBA: BCD points, an extra life at 20,000
static void test_score(void) {
  Game g = flying(0, 1, 0x40, 0x63);
  g.score[0] = 0x01;
  g.score[1] = 0x99;
  g.score[2] = 0x50;
  g.lives = 3;
  addScore(g, 0x75, UNITS);
  TEST_ASSERT_EQUAL_HEX8(0x02, g.score[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, g.score[1]);
  TEST_ASSERT_EQUAL_HEX8(0x25, g.score[2]);
  TEST_ASSERT_EQUAL_UINT8(4, g.lives);
}

// $F569: flying, idle and walking frames
static void test_hero_sprite(void) {
  Game g = flying(0, 1, 0x40, 0x63);
  g.hero.prop = 2;
  heroSprite(g, g.view);
  TEST_ASSERT_EQUAL_HEX8(0xde, g.view.hero_page);
  TEST_ASSERT_EQUAL_HEX8(0xb3, g.view.hero_frame);
  g.hero.standing = 0x40;
  heroSprite(g, g.view);
  TEST_ASSERT_EQUAL_HEX8(0xcc, g.view.hero_frame);
  g.hero.standing = 0x80;
  g.hero.walk = 0;
  heroSprite(g, g.view);
  TEST_ASSERT_EQUAL_HEX8(0xdc, g.view.hero_page);
  TEST_ASSERT_EQUAL_HEX8(0xe5, g.view.hero_frame);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_traces_match_mame);
  RUN_TEST(test_horizontal_exit);
  RUN_TEST(test_water_death);
  RUN_TEST(test_top_clamp);
  RUN_TEST(test_wall_state_on_screen_change);
  RUN_TEST(test_power_out);
  RUN_TEST(test_no_drain_while_waiting);
  RUN_TEST(test_death_sequence);
  RUN_TEST(test_score);
  RUN_TEST(test_hero_sprite);
  return UNITY_END();
}
