#include <unity.h>

#define HAL_VIDEO_NATIVE 1
#include "../../src/emulation/vector2d.cpp"
#include "../../src/machines/llander/llander_layout.cpp"

using vec2d::Line;

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

// native corners of the visible area -> playfield rows 50..237
static void test_playfield(void) {
  Line l[1] = {line(0, 781, 1023, -19, 7)};
  TEST_ASSERT_EQUAL(1, llander_layout::map(l, 1));
  assert_line(l[0], 0, 50, 239, 237, 7);
}

// landing pad labels sit at y 24, below MAME's beam center window
static void test_pad_label_visible(void) {
  const int G = dvg::GLYPH;
  Line l[1] = {line(640, 24, 648, 24, 7 | G, 640, 24)};
  TEST_ASSERT_EQUAL(1, llander_layout::map(l, 1));
  TEST_ASSERT_EQUAL(50 + (781 - 24) * 15 / 64, l[0].y0);
}

static void test_offscreen_dropped(void) {
  Line l[1] = {line(0, 800, 100, 990, 7)};
  TEST_ASSERT_EQUAL(0, llander_layout::map(l, 1));
}

// HUD: 2.25x into the borders, left block on top, right block below
static void test_hud_blocks(void) {
  const int G = dvg::GLYPH;
  Line l[2] = {line(100, 748, 108, 748, 7 | G, 100, 748),
               line(600, 720, 600, 732, 7 | G, 600, 720)};
  TEST_ASSERT_EQUAL(2, llander_layout::map(l, 2));
  assert_line(l[0], 34, 8, 38, 8, 7);     // (760-748)*135/256 = 6
  assert_line(l[1], 34, 262, 34, 255, 7); // 40 and 28 units below top
}

// message run 2x around its center, y around the anchor
static void test_message_centered(void) {
  const int G = dvg::GLYPH;
  Line l[2] = {line(500, 500, 508, 500, 7 | G, 500, 500),
               line(512, 500, 520, 500, 7 | G, 500, 500)};
  TEST_ASSERT_EQUAL(2, llander_layout::map(l, 2));
  // center 510: 490..506 and 514..530 native
  const int row = 50 + (781 - 500) * 15 / 64;
  assert_line(l[0], 490 * 15 / 64, row, 506 * 15 / 64, row, 7);
  assert_line(l[1], 514 * 15 / 64, row, 530 * 15 / 64, row, 7);
}

// a run near the edge shrinks instead of leaving the screen
static void test_message_edge(void) {
  const int G = dvg::GLYPH;
  Line l[1] = {line(1000, 500, 1020, 500, 7 | G, 1000, 500)};
  TEST_ASSERT_EQUAL(1, llander_layout::map(l, 1));
  TEST_ASSERT_TRUE(l[0].x0 >= 0 && l[0].x1 <= 239);
  TEST_ASSERT_TRUE(l[0].x1 - l[0].x0 > 20 * 15 / 64); // still enlarged
}

// lander 2x about its body's bottom center (500, 400), flame included
static void test_lander_doubled(void) {
  Line l[3] = {line(496, 400, 504, 400, 12), line(504, 400, 504, 408, 12),
               line(500, 400, 500, 380, 15)};
  TEST_ASSERT_EQUAL(3, llander_layout::map(l, 3));
  const int row = 50 + (781 - 400) * 15 / 64;
  assert_line(l[0], 492 * 15 / 64, row, 508 * 15 / 64, row, 12);
  assert_line(l[1], 508 * 15 / 64, row, 508 * 15 / 64, 50 + (781 - 416) * 15 / 64, 12);
  assert_line(l[2], 500 * 15 / 64, row, 500 * 15 / 64, 50 + (781 - 360) * 15 / 64, 15);
}

// native HDMI: one scale onto 480 rows, visible corners on the first and
// last column and row
static void test_native_corners(void) {
  const int W = llander_layout::NATIVE_WIDTH, H = 480;
  TEST_ASSERT_EQUAL(614, W);
  Line l[2] = {line(0, 781, 1023, -19, 7), line(-100, 900, 1100, -100, 3)};
  TEST_ASSERT_EQUAL(2, llander_layout::map_native(l, 2));
  assert_line(l[0], 0, 0, W - 1, H - 1, 7);
  for (const Line &k : l) {
    TEST_ASSERT_TRUE(k.x0 >= 0 && k.x0 < W && k.x1 >= 0 && k.x1 < W);
    TEST_ASSERT_TRUE(k.y0 >= 0 && k.y0 < H && k.y1 >= 0 && k.y1 < H);
  }
  Line o[1] = {line(0, 800, 100, 990, 7)};
  TEST_ASSERT_EQUAL(0, llander_layout::map_native(o, 1));
}

// text (HUD and messages) and the lander keep their size and place on
// native, the glyph tag goes
static void test_native_text_kept(void) {
  const int G = dvg::GLYPH;
  Line t[4] = {line(100, 748, 108, 748, 7 | G, 100, 748),
               line(600, 720, 600, 732, 7 | G, 600, 720),
               line(500, 500, 508, 500, 7 | G, 500, 500),
               line(496, 400, 504, 400, 12)};
  Line p[4] = {line(100, 748, 108, 748, 7), line(600, 720, 600, 732, 7),
               line(500, 500, 508, 500, 7), line(496, 400, 504, 400, 12)};
  TEST_ASSERT_EQUAL(4, llander_layout::map_native(t, 4));
  TEST_ASSERT_EQUAL(4, llander_layout::map_native(p, 4));
  for (int i = 0; i < 4; i++) {
    assert_line(t[i], p[i].x0, p[i].y0, p[i].x1, p[i].y1, p[i].color);
  }
  // 8 units at 0.6 px a unit, not 2x
  TEST_ASSERT_TRUE(t[2].x1 - t[2].x0 <= 5);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_playfield);
  RUN_TEST(test_pad_label_visible);
  RUN_TEST(test_offscreen_dropped);
  RUN_TEST(test_hud_blocks);
  RUN_TEST(test_message_centered);
  RUN_TEST(test_message_edge);
  RUN_TEST(test_lander_doubled);
  RUN_TEST(test_native_corners);
  RUN_TEST(test_native_text_kept);
  return UNITY_END();
}
