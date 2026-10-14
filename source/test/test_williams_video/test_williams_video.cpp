#include <string.h>
#include <unity.h>
#include "../../src/machines/williams/williams_video.h"

void setUp(void) {}
void tearDown(void) {}

static const int NATIVE_WIDTH = 292;

static void checkLayout(const ColumnLayout &l, int expectCount) {
  ColumnMap m;
  m.build(l);
  TEST_ASSERT_EQUAL(expectCount, m.count());
  TEST_ASSERT_TRUE(m.left() + m.count() <= ColumnMap::OUT_MAX);
  for (int i = 0; i < m.count(); i++) {
    const int last = m.src(i) + (m.merge(i) ? 1 : 0);
    TEST_ASSERT_TRUE(last < NATIVE_WIDTH);
  }
}

void test_layouts_fit_240(void) {
  checkLayout(DEFENDER_COLUMNS, 239);
  checkLayout(ROBOTRON_COLUMNS, 239);
  checkLayout(JOUST_COLUMNS, 239);
}

void test_defender_drops_3_and_8_of_11(void) {
  ColumnMap m;
  m.build(DEFENDER_COLUMNS);
  // kept 0,1,2 (2 merges 3), 4..7 (7 merges 8), 9, 10, 11 ...
  TEST_ASSERT_EQUAL(2, m.src(2));
  TEST_ASSERT_TRUE(m.merge(2));
  TEST_ASSERT_EQUAL(4, m.src(3));
  TEST_ASSERT_FALSE(m.merge(3));
  TEST_ASSERT_EQUAL(9, m.src(7));
}

void test_pixel_nibbles(void) {
  const uint8_t row[2] = { 0xab, 0xcd };
  TEST_ASSERT_EQUAL_HEX8(0x0a, williamsPixel(row, 0));
  TEST_ASSERT_EQUAL_HEX8(0x0b, williamsPixel(row, 1));
  TEST_ASSERT_EQUAL_HEX8(0x0d, williamsPixel(row, 3));
}

static uint8_t vram[0xc000];
static uint8_t bufA[Scanout::BUF_BYTES], bufB[Scanout::BUF_BYTES];

void test_capture_copies_visible_lines_to_back(void) {
  Scanout s;
  s.attach(bufA, bufB);
  s.clear();
  memset(vram, 0, sizeof(vram));
  vram[3 * 256 + 7] = 0x12;     // byte column 3, line 7 = row 0, col 0
  vram[(3 + 145) * 256 + 246] = 0x34;
  const uint8_t pal[16] = { 9 };
  for (int line = 0; line < Scanout::LINES; line += Scanout::BAND_LINES) {
    s.capture(vram, 3, line, Scanout::BAND_LINES, pal);
  }
  const uint8_t shown = s.front();
  s.flip();
  TEST_ASSERT_NOT_EQUAL(shown, s.front());
  const uint8_t *f = s.frame(s.front());
  TEST_ASSERT_EQUAL_HEX8(0x12, f[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, f[239 * Scanout::COLS + 145]);
  TEST_ASSERT_EQUAL(9, s.palette(s.front(), 100)[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, s.frame(shown)[0]);  // front untouched while capturing
}

void test_double_flip_returns_to_first_buffer(void) {
  Scanout s;
  s.attach(bufA, bufB);
  const uint8_t f0 = s.front();
  s.flip();
  s.flip();
  TEST_ASSERT_EQUAL(f0, s.front());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_layouts_fit_240);
  RUN_TEST(test_defender_drops_3_and_8_of_11);
  RUN_TEST(test_pixel_nibbles);
  RUN_TEST(test_capture_copies_visible_lines_to_back);
  RUN_TEST(test_double_flip_returns_to_first_buffer);
  return UNITY_END();
}
