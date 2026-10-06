#include <unity.h>

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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_plain);
  RUN_TEST(test_small_text_doubled);
  RUN_TEST(test_large_text_kept);
  RUN_TEST(test_small_text_fits);
  return UNITY_END();
}
