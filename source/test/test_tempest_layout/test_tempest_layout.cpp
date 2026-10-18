#include <unity.h>

#include "../../src/emulation/vector2d.cpp"
#include "../../src/machines/tempest/tempest_layout.cpp"

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

static void assert_line(const Line &l, int x0, int y0, int x1, int y1, int color) {
  TEST_ASSERT_EQUAL(x0, l.x0);
  TEST_ASSERT_EQUAL(y0, l.y0);
  TEST_ASSERT_EQUAL(x1, l.x1);
  TEST_ASSERT_EQUAL(y1, l.y1);
  TEST_ASSERT_EQUAL(color, l.color);
}

// MAME: beam (x, y) -> final screen (x - 5, 575 - y) after the AVG axis
// swap and ROT270; visible beam corners land on the panel corners
static void test_corners(void) {
  Line l[1] = {line(5, 575, 575, 0, 7)};
  TEST_ASSERT_EQUAL(1, tempest_layout::map(l, 1, 0));
  assert_line(l[0], 0, 0, 239, 284, 7);
}

// lines outside the visible beam area are dropped or clipped
static void test_clip(void) {
  Line l[2] = {line(0, 10, 4, 10, 7), line(0, 100, 100, 100, 3)};
  TEST_ASSERT_EQUAL(1, tempest_layout::map(l, 2, 0));
  TEST_ASSERT_EQUAL(0, l[0].x0);
  TEST_ASSERT_EQUAL(3, l[0].color);
}

// the beam center stays on the panel center column, also when flipped
static void test_center_and_flip(void) {
  Line l[1] = {line(290, 285, 300, 285, 7)};
  TEST_ASSERT_EQUAL(1, tempest_layout::map(l, 1, 0));
  const int cx = l[0].x0, cy = l[0].y0;
  TEST_ASSERT_INT_WITHIN(1, 119, cx);

  Line f[1] = {line(290, 285, 300, 285, 7)};
  TEST_ASSERT_EQUAL(1, tempest_layout::map(f, 1, tempest_layout::FLIP_X | tempest_layout::FLIP_Y));
  TEST_ASSERT_EQUAL(cx, f[0].x0);
  TEST_ASSERT_EQUAL(cy, f[0].y0);
  TEST_ASSERT_LESS_THAN(cx, f[0].x1); // x mirrored about the center
}

// color RAM bits are inverted: value 0 is white, 0x7 only green (bit 3
// clear), 0xe only the faint red of bit 0
static void test_palette(void) {
  uint16_t pal[tempest_layout::PALETTE_SIZE];
  tempest_layout::palette(pal);
  // MAME levels: bit 0xf3, red bit 0 adds 0x0c; white = (255, 243, 243)
  const uint16_t white = 0xff9e, green = 60 << 5;
  TEST_ASSERT_EQUAL_HEX16((uint16_t)((white >> 8) | (white << 8)), pal[(0x0 << 3) | 7]);
  TEST_ASSERT_EQUAL_HEX16((uint16_t)((green >> 8) | (green << 8)), pal[(0x7 << 3) | 7]);
  const uint16_t faint = pal[(0xe << 3) | 7];
  const uint16_t rgb = (faint >> 8) | (faint << 8);
  TEST_ASSERT_EQUAL(0, rgb & 0x07ff); // no green, no blue
  TEST_ASSERT_LESS_THAN(0x2000, rgb); // red < 8 of 31
  TEST_ASSERT_EQUAL_HEX16(0, pal[(0x0 << 3) | 0]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_corners);
  RUN_TEST(test_clip);
  RUN_TEST(test_center_and_flip);
  RUN_TEST(test_palette);
  return UNITY_END();
}
