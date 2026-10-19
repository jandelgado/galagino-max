#include <unity.h>

#include "../../src/emulation/vector2d.cpp"
#include "../../src/machines/bwidow/bwidow_layout.cpp"
#include "../../src/machines/gravitar/gravitar_layout.h"

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

// visarea corners (0, 0)-(420, 400) -> columns 0..239, rows 29..257
static void test_corners(void) {
  Line l[1] = {line(0, 0, 420, 400, 0x7f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(gravitar_layout::SCREEN, l, 1, 0));
  TEST_ASSERT_EQUAL(0, l[0].x0);
  TEST_ASSERT_EQUAL(29, l[0].y0);
  TEST_ASSERT_EQUAL(239, l[0].x1);
  TEST_ASSERT_EQUAL(257, l[0].y1);
  TEST_ASSERT_EQUAL(0x7f, l[0].color);
}

// beyond x 420 is clipped: Space Duel's wider screen must not leak in
static void test_clip_right(void) {
  Line l[1] = {line(400, 100, 500, 100, 0x2f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(gravitar_layout::SCREEN, l, 1, 0));
  TEST_ASSERT_EQUAL(239, l[0].x1);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_corners);
  RUN_TEST(test_clip_right);
  return UNITY_END();
}
