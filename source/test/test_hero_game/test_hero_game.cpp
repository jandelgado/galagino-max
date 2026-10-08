#include <stdio.h>
#include <unity.h>

#define ENABLE_HERO
#include "../../src/machines/hero/hero_data.cpp"
#include "../../src/machines/hero/hero_room.cpp"
#include "../../src/machines/hero/hero_game.cpp"
#include "hero_traces.h"

using namespace hero_game;

void setUp(void) {}
void tearDown(void) {}

static Input fromSwcha(uint8_t v) {
  Input in = {};
  in.up = !(v & 0x10);
  in.down = !(v & 0x20);
  in.left = !(v & 0x40);
  in.right = !(v & 0x80);
  return in;
}

// every frame of the MAME traces (tools/trace_fixture.py)
static void test_traces_match_mame(void) {
  for (const HeroTrace &t : hero_traces) {
    Game g;
    startLevel(g, 0);
    g.frame = t.frame0;
    if (t.noball) { g.ball_x = hero_room::NO_BALL; }
    for (int i = 0; i < t.rows; i++) {
      const HeroTraceRow &r = t.row[i];
      tick(g, fromSwcha(r.in));
      char msg[64];
      snprintf(msg, sizeof(msg), "%s frame %d", t.name, i);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(r.x, g.hero.x, msg);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(r.y, g.hero.y, msg);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(r.screen, g.screen, msg);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(r.thrust, g.hero.thrust, msg);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(r.standing, g.hero.standing, msg);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(r.power, g.power, msg);
    }
  }
}

// a game in the air on level/screen, respawn over
static Game flying(uint8_t level, uint8_t screen, uint8_t x, uint8_t y) {
  Game g;
  startLevel(g, level);
  g.screen = screen;
  g.furthest = screen;
  g.room = hero_room::decode(level, screen);
  g.ball_x = hero_room::NO_BALL;
  g.respawn = 0;
  g.power_full = 1;
  g.hero.x = x;
  g.hero.y = y;
  g.hero.thrust = 0x1f; // hover
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
  tick(g, in);
  TEST_ASSERT_EQUAL_UINT8(1, g.dead);
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
  g.ball_x = hero_room::NO_BALL;     // start wall blown up
  Input in = {};
  tick(g, in);
  tick(g, in);
  TEST_ASSERT_EQUAL_UINT8(1, g.screen);
  TEST_ASSERT_EQUAL_UINT8(52, g.ball_x);
  g.ball_x = hero_room::NO_BALL; // blown up as well
  g.hero.y = 0x89;
  g.hero.thrust = 0x3f;
  in.up = 1;
  tick(g, in);
  tick(g, in);
  TEST_ASSERT_EQUAL_UINT8(0, g.screen);
  TEST_ASSERT_EQUAL_UINT8(hero_room::NO_BALL, g.ball_x); // saved state of screen 0
}

// $D93C: the last power unit ends the life
static void test_power_out(void) {
  Game g = flying(0, 1, 0x40, 0x63);
  g.power = 1;
  g.frame = 0x3f; // drains when the overscan sees $81 & $3F == 0
  Input in = {};
  tick(g, in);
  tick(g, in);
  TEST_ASSERT_EQUAL_UINT8(0, g.power);
  TEST_ASSERT_EQUAL_UINT8(1, g.dead);
}

// $D7B6 jumps past the drain ($D932) while $AD != 0: waiting at the respawn
// point keeps the power full
static void test_no_drain_while_waiting(void) {
  Game g;
  startLevel(g, 0);
  Input in = {};
  for (int i = 0; i < 400; i++) { tick(g, in); }
  TEST_ASSERT_NOT_EQUAL(0, g.respawn);
  TEST_ASSERT_EQUAL_UINT8(0x51, g.power);
}

// $F569: flying, idle and walking frames
static void test_hero_sprite(void) {
  Game g = flying(0, 1, 0x40, 0x63);
  uint8_t page, frame;
  g.hero.prop = 2;
  heroSprite(g, page, frame);
  TEST_ASSERT_EQUAL_HEX8(0xde, page);
  TEST_ASSERT_EQUAL_HEX8(0xb3, frame);
  g.hero.standing = 0x40;
  heroSprite(g, page, frame);
  TEST_ASSERT_EQUAL_HEX8(0xcc, frame);
  g.hero.standing = 0x80;
  g.hero.walk = 0;
  heroSprite(g, page, frame);
  TEST_ASSERT_EQUAL_HEX8(0xdc, page);
  TEST_ASSERT_EQUAL_HEX8(0xe5, frame);
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
  RUN_TEST(test_hero_sprite);
  return UNITY_END();
}
