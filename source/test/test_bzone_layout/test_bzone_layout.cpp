#include <unity.h>

#define HAL_VIDEO_NATIVE 1
#include "../../src/emulation/vector2d.cpp"
#include "../../src/machines/bzone/bzone_layout.cpp"

using vec2d::Line;

static const int G = avg::GLYPH;

void setUp(void) {}
void tearDown(void) {}

static Line line(int x0, int y0, int x1, int y1, int color, int ax = 0, int ay = 0) {
  Line l = {};
  l.x0 = x0;
  l.y0 = y0;
  l.x1 = x1;
  l.y1 = y1;
  l.color = color;
  l.dx = ax;
  l.dy = ay;
  return l;
}

static void assert_line(const Line &l, int x0, int y0, int x1, int y1, int color) {
  TEST_ASSERT_EQUAL(x0, l.x0);
  TEST_ASSERT_EQUAL(y0, l.y0);
  TEST_ASSERT_EQUAL(x1, l.x1);
  TEST_ASSERT_EQUAL(y1, l.y1);
  TEST_ASSERT_EQUAL(color, l.color);
}

// visible corners -> columns 0..239, rows 51..238
static void test_plain(void) {
  Line l[2] = {line(32, 0, 543, 399, 7), line(0, 10, 20, 10, 7)};
  TEST_ASSERT_EQUAL(1, bzone_layout::map(l, 2));
  assert_line(l[0], 0, 51, 239, 238, 7);
}

// half size text (6 units high) grows 2x around its center, up from the
// baseline
static void test_small_text_doubled(void) {
  Line l[2] = {line(100, 100, 100, 94, 7 | G, 100, 100), line(112, 100, 116, 94, 7 | G, 100, 100)};
  TEST_ASSERT_EQUAL(2, bzone_layout::map(l, 2));
  assert_line(l[0], 28, 97, 28, 92, 7);  // native (92,100)-(92,88)
  assert_line(l[1], 39, 97, 43, 92, 7);  // native (116,100)-(124,88)
}

// full size text (12 units) stays
static void test_large_text_kept(void) {
  Line l[1] = {line(100, 100, 100, 88, 7 | G, 100, 100)};
  TEST_ASSERT_EQUAL(1, bzone_layout::map(l, 1));
  assert_line(l[0], 31, 97, 31, 92, 7);
}

// near the left edge the scale shrinks so the run stays visible
static void test_small_text_fits(void) {
  Line l[2] = {line(40, 100, 40, 94, 7 | G, 40, 100), line(60, 100, 60, 94, 7 | G, 40, 100)};
  TEST_ASSERT_EQUAL(2, bzone_layout::map(l, 2));
  assert_line(l[0], 0, 97, 0, 93, 7);   // native (33,100)-(33,90)
  assert_line(l[1], 16, 97, 16, 93, 7); // native (67,100)-(67,90)
}

// native HDMI: 1/16 units, one scale onto 480 rows: visible corners on the
// first and last column and row
static void test_native_corners(void) {
  const int F = bzone_layout::NATIVE_FRAC, W = bzone_layout::NATIVE_WIDTH, H = 480;
  TEST_ASSERT_EQUAL(614, W);
  Line l[2] = {line(32 << F, 0, 543 << F, 399 << F, 7),
               line(-100, (399 << F) + 100, (543 << F) + 100, -100, 3)};
  TEST_ASSERT_EQUAL(2, bzone_layout::map_native(l, 2));
  assert_line(l[0], 0, 0, W - 1, H - 1, 7);
  for (const Line &k : l) {
    TEST_ASSERT_TRUE(k.x0 >= 0 && k.x0 < W && k.x1 >= 0 && k.x1 < W);
    TEST_ASSERT_TRUE(k.y0 >= 0 && k.y0 < H && k.y1 >= 0 && k.y1 < H);
  }
  // left of the Mathbox window: dropped
  Line o[1] = {line(0, 10 << F, 20 << F, 10 << F, 7)};
  TEST_ASSERT_EQUAL(0, bzone_layout::map_native(o, 1));
}

// the red overlay ends at y 80: first green row on a strip edge
static void test_bzone_native_red_strips(void) {
  const int F = bzone_layout::NATIVE_FRAC;
  Line l[2] = {line(100 << F, 80 << F, 200 << F, 80 << F, 7),
               line(100 << F, (80 << F) - 1, 200 << F, (80 << F) - 1, 7)};
  TEST_ASSERT_EQUAL(2, bzone_layout::map_native(l, 2));
  TEST_ASSERT_EQUAL(bzone_layout::RED_STRIPS_NATIVE * 8, l[0].y0);
  TEST_ASSERT_LESS_THAN(bzone_layout::RED_STRIPS_NATIVE * 8, l[1].y0);
}

// half size text keeps its size on native, the glyph tag goes
static void test_native_text_kept(void) {
  const int F = bzone_layout::NATIVE_FRAC;
  Line t[2] = {line(100 << F, 100 << F, 100 << F, 94 << F, 7 | G, 100 << F, 100 << F),
               line(112 << F, 100 << F, 116 << F, 94 << F, 7 | G, 100 << F, 100 << F)};
  Line p[2] = {line(100 << F, 100 << F, 100 << F, 94 << F, 7),
               line(112 << F, 100 << F, 116 << F, 94 << F, 7)};
  TEST_ASSERT_EQUAL(2, bzone_layout::map_native(t, 2));
  TEST_ASSERT_EQUAL(2, bzone_layout::map_native(p, 2));
  for (int i = 0; i < 2; i++) {
    assert_line(t[i], p[i].x0, p[i].y0, p[i].x1, p[i].y1, 7);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_plain);
  RUN_TEST(test_small_text_doubled);
  RUN_TEST(test_large_text_kept);
  RUN_TEST(test_small_text_fits);
  RUN_TEST(test_native_corners);
  RUN_TEST(test_bzone_native_red_strips);
  RUN_TEST(test_native_text_kept);
  return UNITY_END();
}
