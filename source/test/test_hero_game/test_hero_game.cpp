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
static const int SWCHB = 130;

// bits of RAM address `a` the remake reproduces
static uint8_t mask(const Game &g, uint8_t a) {
  switch (a) {
  case 0x84: return g.over ? 0 : 0xff; // frozen game: demo input from past the ROM
  case 0xbd: return 0;                 // kernel scratch at the dump
  case 0xd1: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: return 0x80;
  case 0xe2: case 0xe3: case 0xe4: case 0xf4: return 0xc0;
  default: return 0xff;
  }
}

static void load(Game &g, const uint8_t *rec) {
  powerOn(g);
  for (int a = 0x80; a <= 0xff; a++) {
    uint8_t *f = ram(g, a);
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
  in.start = !(rec[SWCHB] & 1);
  in.select = !(rec[SWCHB] & 2);
  return in;
}

// applies the next delta record of a trace to rec
static const uint8_t *nextRecord(const uint8_t *d, uint8_t *rec) {
  for (int n = *d++; n > 0; n--, d += 2) { rec[d[0]] = d[1]; }
  return d;
}

// every modelled RAM byte, every frame of the MAME traces (tools/trace_fixture.py)
static void test_traces_match_mame(void) {
  for (const HeroTrace &t : hero_traces) {
    static Game g;
    uint8_t rec[HERO_REC] = {};
    const uint8_t *d = nextRecord(t.delta, rec);
    load(g, rec);
    for (int i = 1; i < t.rows; i++) {
      d = nextRecord(d, rec);
      tick(g, inputOf(rec));
      for (int a = 0x80; a <= 0xff; a++) {
        const uint8_t *f = ram(g, a);
        if (!f) { continue; }
        const uint8_t m = mask(g, a);
        char msg[64];
        snprintf(msg, sizeof(msg), "%s frame %d, $%02X", t.name, i, a);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(rec[a - 0x80] & m, *f & m, msg);
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
  powerOn(g);
  Input in = {};
  in.start = 1;
  tick(g, in);
  in.start = 0;
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
