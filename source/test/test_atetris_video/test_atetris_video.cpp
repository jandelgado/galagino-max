#include <unity.h>

#include "../../src/machines/atetris/atetris_video.h"

using namespace atetris_video;

void setUp(void) {}
void tearDown(void) {}

// tile 0: x % 7 = 0,1,2,3,4,5,6,0 -> drop bits 2, 5. tile 1: x 8..15 -> drop bits 1, 4
static void test_scale_spans(void) {
  Span s[VIS_COLS];
  buildSpans(FIT_SCALE, s);
  int kept = 0;
  for (int tx = 0; tx < VIS_COLS; tx++) {
    kept += __builtin_popcount(s[tx].keep);
  }
  TEST_ASSERT_EQUAL(OUT_WIDTH, kept);
  TEST_ASSERT_EQUAL_HEX8(0xdb, s[0].keep);
  TEST_ASSERT_EQUAL_HEX8(0xed, s[1].keep);
  TEST_ASSERT_EQUAL_UINT8(6, s[1].out);
}

// game layout: native x 0..223, centered: 8 black columns each side
static void test_crop_spans(void) {
  Span s[VIS_COLS];
  buildSpans(FIT_CROP, s);
  TEST_ASSERT_EQUAL_UINT8(8, s[0].out);
  TEST_ASSERT_EQUAL_HEX8(0xff, s[CROP_COLS - 1].keep);
  TEST_ASSERT_EQUAL_UINT8(8 + 27 * 8, s[CROP_COLS - 1].out);
  TEST_ASSERT_EQUAL_HEX8(0x00, s[CROP_COLS].keep);
}

static void test_rgb332(void) {
  TEST_ASSERT_EQUAL_HEX16(0xffff, rgb332(0xff));
  TEST_ASSERT_EQUAL_HEX16(0x00f8, rgb332(0xe0)); // red 0xff -> 0xf800, swapped
  TEST_ASSERT_EQUAL_HEX16(0x1f00, rgb332(0x03)); // blue 0xff -> 0x001f, swapped
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_scale_spans);
  RUN_TEST(test_crop_spans);
  RUN_TEST(test_rgb332);
  return UNITY_END();
}
