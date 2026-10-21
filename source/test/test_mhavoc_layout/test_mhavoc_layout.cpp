#include <unity.h>

#include "../../src/emulation/vector2d.cpp"
#include "../../src/machines/mhavoc/mhavoc_layout.cpp"

using vec2d::Line;

void setUp(void) {}
void tearDown(void) {}

static Line glyph(int x0, int y0, int x1, int y1, int ax, int ay, uint8_t color = 0x2d) {
  Line l = {};
  l.x0 = x0, l.y0 = y0, l.x1 = x1, l.y1 = y1;
  l.dx = ax, l.dy = ay;
  l.color = color | avg::GLYPH;
  return l;
}

static int width(const Line *l, int n) {
  int lo = 9999, hi = -9999;
  for (int i = 0; i < n; i++) {
    lo = l[i].x0 < lo ? l[i].x0 : lo, lo = l[i].x1 < lo ? l[i].x1 : lo;
    hi = l[i].x0 > hi ? l[i].x0 : hi, hi = l[i].x1 > hi ? l[i].x1 : hi;
  }
  return hi - lo;
}

static int height(const Line *l, int n) {
  int lo = 9999, hi = -9999;
  for (int i = 0; i < n; i++) {
    lo = l[i].y0 < lo ? l[i].y0 : lo, lo = l[i].y1 < lo ? l[i].y1 : lo;
    hi = l[i].y0 > hi ? l[i].y0 : hi, hi = l[i].y1 > hi ? l[i].y1 : hi;
  }
  return hi - lo;
}

// a 5 unit tall text run (most Major Havoc text) grows by half about its
// box center; color kept, GLYPH flag cleared
static void test_small_text_enlarged(void) {
  Line l[2] = {glyph(100, 100, 130, 100, 100, 100), glyph(100, 95, 130, 95, 100, 100)};
  mhavoc_layout::enlarge_text(l, 2);
  TEST_ASSERT_INT_WITHIN(1, 45, width(l, 2));
  TEST_ASSERT_INT_WITHIN(1, 7, height(l, 2));
  TEST_ASSERT_INT_WITHIN(1, 115, (l[0].x0 + l[0].x1) / 2);
  TEST_ASSERT_EQUAL_UINT8(0x2d, l[0].color);
  TEST_ASSERT_EQUAL_UINT8(0x2d, l[1].color);
}

// 9 unit tall text (oxygen and reactor counters) stays as is
static void test_large_text_kept(void) {
  Line l[2] = {glyph(100, 100, 130, 100, 100, 100), glyph(100, 91, 130, 91, 100, 100)};
  mhavoc_layout::enlarge_text(l, 2);
  TEST_ASSERT_EQUAL(30, width(l, 2));
  TEST_ASSERT_EQUAL(9, height(l, 2));
  TEST_ASSERT_EQUAL_UINT8(0x2d, l[0].color);
}

// runs with different anchors scale separately; other lines are untouched
static void test_runs_and_vectors(void) {
  Line l[3] = {glyph(10, 50, 40, 50, 10, 50), {}, glyph(200, 50, 260, 46, 200, 50)};
  l[1].x0 = 5, l[1].y0 = 5, l[1].x1 = 295, l[1].y1 = 255, l[1].color = 0x11;
  mhavoc_layout::enlarge_text(l, 3);
  TEST_ASSERT_EQUAL(5, l[1].x0);
  TEST_ASSERT_EQUAL(255, l[1].y1);
  TEST_ASSERT_EQUAL_UINT8(0x11, l[1].color);
  TEST_ASSERT_INT_WITHIN(1, 45, width(l, 1));
  TEST_ASSERT_INT_WITHIN(1, 90, width(l + 2, 1));
}

// enlarged text is shifted back inside the visarea (0..300)
static void test_edge_text_kept_visible(void) {
  Line l[2] = {glyph(0, 30, 40, 30, 0, 30), glyph(270, 30, 300, 25, 270, 30)};
  mhavoc_layout::enlarge_text(l, 2);
  TEST_ASSERT_EQUAL(0, l[0].x0 < l[0].x1 ? l[0].x0 : l[0].x1);
  TEST_ASSERT_EQUAL(300, l[1].x0 > l[1].x1 ? l[1].x0 : l[1].x1);
}

static int lo_x(const Line *l, int n) {
  int v = 9999;
  for (int i = 0; i < n; i++) v = l[i].x0 < v ? l[i].x0 : v, v = l[i].x1 < v ? l[i].x1 : v;
  return v;
}
static int hi_x(const Line *l, int n) {
  int v = -9999;
  for (int i = 0; i < n; i++) v = l[i].x0 > v ? l[i].x0 : v, v = l[i].x1 > v ? l[i].x1 : v;
  return v;
}
static int lo_y(const Line *l, int n) {
  int v = 9999;
  for (int i = 0; i < n; i++) v = l[i].y0 < v ? l[i].y0 : v, v = l[i].y1 < v ? l[i].y1 : v;
  return v;
}
static int hi_y(const Line *l, int n) {
  int v = -9999;
  for (int i = 0; i < n; i++) v = l[i].y0 > v ? l[i].y0 : v, v = l[i].y1 > v ? l[i].y1 : v;
  return v;
}

// OXYGEN and REACTOR (maze HUD): neighbors on one row keep a gap, the one
// at the right edge grows only as far as the edge
static void test_row_neighbors_keep_gap(void) {
  Line l[4] = {glyph(185, 44, 223, 44, 185, 44), glyph(185, 39, 223, 39, 185, 44),
               glyph(248, 44, 292, 44, 247, 44), glyph(248, 39, 292, 39, 247, 44)};
  mhavoc_layout::enlarge_text(l, 4);
  TEST_ASSERT_GREATER_OR_EQUAL(4, lo_x(l + 2, 2) - hi_x(l, 2));
  TEST_ASSERT_LESS_OR_EQUAL(300, hi_x(l + 2, 2));
  TEST_ASSERT_GREATER_THAN(44, hi_x(l + 2, 2) - lo_x(l + 2, 2));
  TEST_ASSERT_GREATER_THAN(38, hi_x(l, 2) - lo_x(l, 2));
}

// rows 8 units apart (attract footer) grow without touching
static void test_close_rows_keep_gap(void) {
  Line l[4] = {glyph(96, 217, 208, 217, 99, 217), glyph(96, 212, 208, 212, 99, 217),
               glyph(94, 225, 209, 225, 94, 225), glyph(94, 220, 209, 220, 94, 225)};
  mhavoc_layout::enlarge_text(l, 4);
  TEST_ASSERT_GREATER_OR_EQUAL(1, lo_y(l + 2, 2) - hi_y(l, 2));
  TEST_ASSERT_GREATER_THAN(5, hi_y(l, 2) - lo_y(l, 2));
}

// a glyph run right next to another on its row (the (c) sign before
// MCMLXXXIII) scales with it as one text line
static void test_adjacent_runs_merge(void) {
  Line l[2] = {glyph(97, 233, 105, 226, 97, 233), glyph(107, 232, 208, 228, 107, 232)};
  mhavoc_layout::enlarge_text(l, 2);
  TEST_ASSERT_GREATER_OR_EQUAL(1, lo_x(l + 1, 1) - hi_x(l, 1));
  TEST_ASSERT_GREATER_THAN(140, hi_x(l + 1, 1) - lo_x(l, 1));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_small_text_enlarged);
  RUN_TEST(test_large_text_kept);
  RUN_TEST(test_runs_and_vectors);
  RUN_TEST(test_edge_text_kept_visible);
  RUN_TEST(test_row_neighbors_keep_gap);
  RUN_TEST(test_close_rows_keep_gap);
  RUN_TEST(test_adjacent_runs_merge);
  return UNITY_END();
}
