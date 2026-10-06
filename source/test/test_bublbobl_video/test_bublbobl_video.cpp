#include <unity.h>
#include "../../src/machines/bublbobl/bublbobl_video.h"

using namespace bublbobl_video;

// PROM 0x80..0xff as measured from a71-25.41
static const uint8_t PROM[128] = {
  0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0x0,
  0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0x1,
  0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0x2,
  0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0xc,0x3,
  0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,
  0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,
  4,4,4,4,5,5,5,5,6,6,6,6,7,7,7,7,
  4,4,4,4,5,5,5,5,6,6,6,6,7,7,7,7,
};

void setUp() {}
void tearDown() {}

void test_keep_map() {
  TEST_ASSERT_EQUAL_UINT8(0, keep_x(0));
  TEST_ASSERT_EQUAL_UINT8(6, keep_x(6));
  TEST_ASSERT_EQUAL_UINT8(8, keep_x(7));
  TEST_ASSERT_EQUAL_UINT8(254, keep_x(SCALE_WIDTH - 1));
  for (uint16_t c = 0; c < SCALE_WIDTH; c++) {
    TEST_ASSERT_NOT_EQUAL(7, keep_x(c) & 7);
  }
}

void test_crop_map() {
  TEST_ASSERT_EQUAL_UINT8(8, crop_x(0));
  TEST_ASSERT_EQUAL_UINT8(247, crop_x(CROP_WIDTH - 1));
  TEST_ASSERT_EQUAL_UINT16(256, CROP_WIDTH + 2 * CROP_LEFT);
}

void test_first_new_column() {
  TEST_ASSERT_EQUAL_UINT8(30, first_new_column(PROM, 0));
  TEST_ASSERT_EQUAL_UINT8(30, first_new_column(PROM, 3));
  TEST_ASSERT_EQUAL_UINT8(0, first_new_column(PROM, 4));
  TEST_ASSERT_EQUAL_UINT8(32, first_new_column(PROM, 6));
}

void test_chain_skips_empty_objects() {
  // object A type 4 at x 40, (empty object skipped, sx unchanged), object B type 6
  ObjPos a = {0, 40};
  int16_t sx = next_sx(a, first_new_column(PROM, 4));
  TEST_ASSERT_EQUAL_INT16(56, sx);
  ObjPos b = {sx, 200};
  TEST_ASSERT_EQUAL_INT16(56, object_x(b, first_new_column(PROM, 6), 0));
  TEST_ASSERT_EQUAL_INT16(72, next_sx(b, first_new_column(PROM, 6)));
}

void test_x_negative() {
  ObjPos p = {0, (int16_t)(250 - 256)};
  TEST_ASSERT_EQUAL_INT16(-6, object_x(p, 0, 5));
}

void test_rows_for_strip_aligned() {
  uint8_t yc[2];
  TEST_ASSERT_EQUAL_UINT8(1, rows_for_strip(16, 32, yc));
  TEST_ASSERT_EQUAL_UINT8(2, yc[0]);
}

void test_rows_for_strip_wrap() {
  uint8_t yc[2];
  // sy 0xfc: row 0 covers 252..3, strip line0 0 overlaps rows 0 and 1
  TEST_ASSERT_EQUAL_UINT8(2, rows_for_strip(0xfc, 0, yc));
  TEST_ASSERT_EQUAL_UINT8(0, yc[0]);
  TEST_ASSERT_EQUAL_UINT8(1, yc[1]);
  // sy 4, line0 0: row 31 covers 252..3 and row 0 covers 4..11
  TEST_ASSERT_EQUAL_UINT8(2, rows_for_strip(4, 0, yc));
  TEST_ASSERT_EQUAL_UINT8(31, yc[0]);
  TEST_ASSERT_EQUAL_UINT8(0, yc[1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_keep_map);
  RUN_TEST(test_crop_map);
  RUN_TEST(test_first_new_column);
  RUN_TEST(test_chain_skips_empty_objects);
  RUN_TEST(test_x_negative);
  RUN_TEST(test_rows_for_strip_aligned);
  RUN_TEST(test_rows_for_strip_wrap);
  return UNITY_END();
}
