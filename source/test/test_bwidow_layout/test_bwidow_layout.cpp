#include <unity.h>

#include "../../src/emulation/vector2d.cpp"
#include "../../src/machines/bwidow/bwidow_layout.cpp"
#include "../../src/machines/bwidow/bwidow_game_layout.h"
#include "../../src/machines/bwidow/bwidow_inputs.h"

using vec2d::Line;

void setUp(void) {}
void tearDown(void) {}

static Line line(int x0, int y0, int x1, int y1, int color) {
  Line l = {};
  l.x0 = x0;
  l.y0 = y0;
  l.x1 = x1;
  l.y1 = y1;
  l.color = color;
  return l;
}

// visarea corners (0, 0)-(480, 440) -> columns 0..239, rows 34..253
static void test_corners(void) {
  Line l[1] = {line(0, 0, 480, 440, 0x7f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(bwidow_game_layout::SCREEN, l, 1, 0));
  TEST_ASSERT_EQUAL(0, l[0].x0);
  TEST_ASSERT_EQUAL(34, l[0].y0);
  TEST_ASSERT_EQUAL(239, l[0].x1);
  TEST_ASSERT_EQUAL(253, l[0].y1);
  TEST_ASSERT_EQUAL(0x7f, l[0].color);
}

static void test_clip_right(void) {
  Line l[1] = {line(400, 100, 600, 100, 0x2f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(bwidow_game_layout::SCREEN, l, 1, 0));
  TEST_ASSERT_EQUAL(239, l[0].x1);
}

// stick alone moves, FIRE also fires that way; START reaches IN4 b5
static void test_inputs(void) {
  TwinStick sticks;
  uint16_t s = sticks.update(BOARD_KEY_UP | BOARD_KEY_LEFT);
  TEST_ASSERT_EQUAL_HEX8(0xff & ~(BW_UP | BW_LEFT), bwidow_in3(s));
  TEST_ASSERT_EQUAL_HEX8(0xff, bwidow_in4(s, 0));

  s = sticks.update(BOARD_KEY_RIGHT | BOARD_KEY_FIRE);
  TEST_ASSERT_EQUAL_HEX8(0xff & ~BW_RIGHT, bwidow_in3(s));
  TEST_ASSERT_EQUAL_HEX8(0xff & ~BW_RIGHT, bwidow_in4(s, 0));

  // aim stays right while FIRE is held
  s = sticks.update(BOARD_KEY_DOWN | BOARD_KEY_FIRE);
  TEST_ASSERT_EQUAL_HEX8(0xff & ~BW_DOWN, bwidow_in3(s));
  TEST_ASSERT_EQUAL_HEX8(0xff & ~BW_RIGHT, bwidow_in4(s, 0));

  TEST_ASSERT_EQUAL_HEX8(0xff & ~BW_START1, bwidow_in4(sticks.update(0), BOARD_KEY_START));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_corners);
  RUN_TEST(test_clip_right);
  RUN_TEST(test_inputs);
  return UNITY_END();
}
