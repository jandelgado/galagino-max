#include <unity.h>

#include "../../src/emulation/vector2d.cpp"
#include "../../src/machines/bwidow/bwidow_layout.cpp"
#include "../../src/machines/spacduel/spacduel_layout.h"

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

static uint16_t unswap(uint16_t c) { return (c >> 8) | (c << 8); }

// visarea corners (0, 0)-(540, 400) -> columns 0..239, rows 55..232
static void test_corners(void) {
  Line l[1] = {line(0, 0, 540, 400, 0x7f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(spacduel_layout::SCREEN, l, 1, 0));
  TEST_ASSERT_EQUAL(0, l[0].x0);
  TEST_ASSERT_EQUAL(55, l[0].y0);
  TEST_ASSERT_EQUAL(239, l[0].x1);
  TEST_ASSERT_EQUAL(232, l[0].y1);
  TEST_ASSERT_EQUAL(0x7f, l[0].color);
}

// lines outside the visible area are dropped or clipped
static void test_clip(void) {
  Line l[2] = {line(-20, 10, -5, 10, 0x1f), line(-10, 100, 100, 100, 0x2f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(spacduel_layout::SCREEN, l, 2, 0));
  TEST_ASSERT_EQUAL(0, l[0].x0);
  TEST_ASSERT_EQUAL(0x2f, l[0].color);
}

// flip mirrors about the beam center (270, 200)
static void test_flip(void) {
  Line l[1] = {line(270, 200, 370, 250, 0x7f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(spacduel_layout::SCREEN, l, 1, bwidow_layout::FLIP_X | bwidow_layout::FLIP_Y));
  Line r[1] = {line(270, 200, 170, 150, 0x7f)};
  TEST_ASSERT_EQUAL(1, bwidow_layout::map(spacduel_layout::SCREEN, r, 1, 0));
  TEST_ASSERT_EQUAL(r[0].x0, l[0].x0);
  TEST_ASSERT_EQUAL(r[0].y0, l[0].y0);
  TEST_ASSERT_EQUAL(r[0].x1, l[0].x1);
  TEST_ASSERT_EQUAL(r[0].y1, l[0].y1);
}

// color111: bit 2 red, 1 green, 0 blue; level 0 and color 0 are black
static void test_palette(void) {
  uint16_t pal[bwidow_layout::PALETTE_SIZE];
  bwidow_layout::palette(pal);
  TEST_ASSERT_EQUAL_HEX16(0, pal[0x40]); // red, level 0
  TEST_ASSERT_EQUAL_HEX16(0, pal[0x0f]); // black, level 15
  const uint16_t red = unswap(pal[0x4f]);
  TEST_ASSERT_NOT_EQUAL(0, red & 0xf800);
  TEST_ASSERT_EQUAL_HEX16(0, red & 0x07ff);
  const uint16_t cyan = unswap(pal[0x3f]);
  TEST_ASSERT_EQUAL_HEX16(0, cyan & 0xf800);
  TEST_ASSERT_NOT_EQUAL(0, cyan & 0x07e0);
  TEST_ASSERT_NOT_EQUAL(0, cyan & 0x001f);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_corners);
  RUN_TEST(test_clip);
  RUN_TEST(test_flip);
  RUN_TEST(test_palette);
  return UNITY_END();
}
