#include <unity.h>
#include <string.h>
#include "../../src/hal/rp2350/dvi_line.h"

using namespace hal::dvi;

void setUp(void) {}
void tearDown(void) {}

static void test_swap565(void) {
  TEST_ASSERT_EQUAL_HEX16(0x3412, swap565(0x1234));
  static uint16_t in[1920], out[1920];
  for (uint32_t i = 0; i < 1920; i++) {
    in[i] = (uint16_t)(i * 0x0101 + 7);
  }
  copySwapped(in, out, 1920);
  for (uint32_t i = 0; i < 1920; i++) {
    TEST_ASSERT_EQUAL_HEX16(swap565(in[i]), out[i]);
  }
}

static void test_blend565(void) {
  TEST_ASSERT_EQUAL_HEX16(0xF800, blend565(0xF800, 0x001F, 0));
  TEST_ASSERT_EQUAL_HEX16(0x001F, blend565(0xF800, 0x001F, 4));
  TEST_ASSERT_EQUAL_HEX16(0xB807, blend565(0xF800, 0x001F, 1));
  TEST_ASSERT_EQUAL_HEX16(0x780F, blend565(0xF800, 0x001F, 2));
  TEST_ASSERT_EQUAL_HEX16(0x03E0, blend565(0x07E0, 0x0000, 2));
}

static void assertRow(uint32_t line, uint32_t rows, uint32_t lines, uint32_t row, uint32_t q) {
  uint32_t gotQ = 99;
  TEST_ASSERT_EQUAL_UINT32(row, scaleRow(line, rows, lines, &gotQ));
  TEST_ASSERT_EQUAL_UINT32(q, gotQ);
}

static void test_scale_row_288_on_432(void) {
  TEST_ASSERT_EQUAL_UINT32(432, scaledLines(288));
  assertRow(0, 288, 432, 0, 0);
  assertRow(1, 288, 432, 0, 2);
  assertRow(2, 288, 432, 1, 0);
  assertRow(431, 288, 432, 287, 0);
}

static void test_scale_row_240_on_480(void) {
  TEST_ASSERT_EQUAL_UINT32(480, scaledLines(240));
  assertRow(1, 240, 480, 0, 0);
  assertRow(2, 240, 480, 1, 0);
  for (uint32_t line = 0; line < 480; line++) {
    uint32_t q;
    scaleRow(line, 240, 480, &q);
    TEST_ASSERT_EQUAL_UINT32(0, q);
  }
}

static void test_scale_row_never_reads_past_last_row(void) {
  const uint32_t heights[] = {240, 288};
  for (uint32_t rows : heights) {
    const uint32_t lines = scaledLines(rows);
    for (uint32_t line = 0; line < lines; line++) {
      uint32_t q;
      const uint32_t row = scaleRow(line, rows, lines, &q);
      TEST_ASSERT_TRUE(row + (q ? 1 : 0) < rows);
    }
  }
}

static void test_line_1x(void) {
  const uint16_t row[4] = {1, 2, 3, 4};
  uint32_t out[2];
  line1x(row, out, 4, Mirror::OFF);
  TEST_ASSERT_EQUAL_HEX32(0x00020001, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00040003, out[1]);
  line1x(row, out, 4, Mirror::ON);
  TEST_ASSERT_EQUAL_HEX32(0x00030004, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00010002, out[1]);
}

static void test_line_2x(void) {
  const uint16_t a[2] = {1, 2};
  uint32_t out[2];
  line2x(a, a, 0, out, 2, Mirror::OFF);
  TEST_ASSERT_EQUAL_HEX32(0x00010001, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00020002, out[1]);
  line2x(a, a, 0, out, 2, Mirror::ON);
  TEST_ASSERT_EQUAL_HEX32(0x00020002, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00010001, out[1]);
  const uint16_t red[1] = {0xF800}, blue[1] = {0x001F};
  line2x(red, blue, 2, out, 1, Mirror::OFF);
  TEST_ASSERT_EQUAL_HEX32(0x780F780F, out[0]);
}

static void test_line_2x_224_leaves_border_black(void) {
  static uint16_t row[224];
  for (uint32_t i = 0; i < 224; i++) {
    row[i] = 0xFFFF;
  }
  // 448 of 640 pixels, centered: 96 pixels = 48 words of border each side
  static uint32_t line[320];
  memset(line, 0, sizeof(line));
  line2x(row, row, 0, line + 48, 224, Mirror::OFF);
  for (uint32_t i = 0; i < 48; i++) {
    TEST_ASSERT_EQUAL_HEX32(0, line[i]);
    TEST_ASSERT_EQUAL_HEX32(0, line[320 - 48 + i]);
  }
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, line[48]);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, line[48 + 223]);
}

// galagino's flipY turns the screen by 180 degrees (SPI TFT: MADCTL MY|MX),
// flipX mirrors it horizontally on top of that
static void test_orientation_follows_panel_flips(void) {
  TEST_ASSERT_TRUE(orientation(0, 0).x == Mirror::OFF && orientation(0, 0).y == Mirror::OFF);
  TEST_ASSERT_TRUE(orientation(1, 0).x == Mirror::ON && orientation(1, 0).y == Mirror::ON);
  TEST_ASSERT_TRUE(orientation(0, 1).x == Mirror::ON && orientation(0, 1).y == Mirror::OFF);
  TEST_ASSERT_TRUE(orientation(1, 1).x == Mirror::OFF && orientation(1, 1).y == Mirror::ON);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_swap565);
  RUN_TEST(test_blend565);
  RUN_TEST(test_scale_row_288_on_432);
  RUN_TEST(test_scale_row_240_on_480);
  RUN_TEST(test_scale_row_never_reads_past_last_row);
  RUN_TEST(test_line_1x);
  RUN_TEST(test_line_2x);
  RUN_TEST(test_line_2x_224_leaves_border_black);
  RUN_TEST(test_orientation_follows_panel_flips);
  return UNITY_END();
}
