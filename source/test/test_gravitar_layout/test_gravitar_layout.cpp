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

// native HDMI: 1/16 units, corners (0, 0)-(6720, 6400) -> columns 0..498 of 500, rows 2..477
static void test_native_corners(void) {
  const int16_t r = 420 << bwidow_layout::NATIVE_FRAC, b = 400 << bwidow_layout::NATIVE_FRAC;
  const int W = gravitar_layout::NATIVE_WIDTH, H = 480;
  Line l[2] = {line(0, 0, r, b, 0x7f), line(-100, b + 100, r + 100, -100, 0x7f)};
  TEST_ASSERT_EQUAL(2, bwidow_layout::map(gravitar_layout::SCREEN_NATIVE, l, 2, 0));
  TEST_ASSERT_EQUAL(0, l[0].x0);
  TEST_ASSERT_EQUAL(2, l[0].y0);
  TEST_ASSERT_EQUAL(498, l[0].x1);
  TEST_ASSERT_EQUAL(477, l[0].y1);
  TEST_ASSERT_EQUAL(0, W & 1);
  TEST_ASSERT_TRUE(l[0].x1 < W);
  for (const Line &k : l) {
    TEST_ASSERT_TRUE(k.x0 >= 0 && k.x0 < W && k.x1 >= 0 && k.x1 < W);
    TEST_ASSERT_TRUE(k.y0 >= 0 && k.y0 < H && k.y1 >= 0 && k.y1 < H);
  }
  Line f[1] = {line(0, 0, r, b, 0x7f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(gravitar_layout::SCREEN_NATIVE, f, 1,
                                          bwidow_layout::FLIP_X | bwidow_layout::FLIP_Y));
  TEST_ASSERT_EQUAL(498, f[0].x0);
  TEST_ASSERT_EQUAL(477, f[0].y0);
  TEST_ASSERT_EQUAL(0, f[0].x1);
  TEST_ASSERT_EQUAL(2, f[0].y1);
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
  RUN_TEST(test_native_corners);
  RUN_TEST(test_clip_right);
  return UNITY_END();
}
