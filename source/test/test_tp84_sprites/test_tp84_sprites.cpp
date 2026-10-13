#include <unity.h>

#include "../../src/machines/tp84/tp84_hw.h"

using namespace tp84_hw;

static SpriteBands b;
static Sprite out[MAX_SPRITES];

void setUp(void) { b.reset(); }
void tearDown(void) {}

static void put(uint8_t slot, uint8_t x, uint8_t code, uint8_t attr, uint8_t sy, uint8_t line) {
  b.write(slot * 4 + 0, x, line);
  b.write(slot * 4 + 1, code, line);
  b.write(slot * 4 + 2, attr, line);
  b.write(slot * 4 + 3, sy, line);
}

static void test_midscreen_rewrite_splits_slot(void) {
  b.beginFrame();
  put(3, 10, 20, 0x40, 100, 241);
  put(3, 50, 77, 0x41, 60, 132);    // multiplexed: same slot reused lower on screen
  uint8_t n = b.finish(out);
  int k = 0;
  Sprite s[2];
  for (int i = 0; i < n; i++)
    if (out[i].slot == 3) {
      TEST_ASSERT_TRUE(k < 2);
      s[k++] = out[i];
    }
  TEST_ASSERT_EQUAL_INT(2, k);
  // lines [16,132) show the old content, [132,240) the new one
  const Sprite &olds = s[0].code == 20 ? s[0] : s[1];
  const Sprite &news = s[0].code == 20 ? s[1] : s[0];
  TEST_ASSERT_EQUAL_UINT8(VIS_TOP, olds.top);
  TEST_ASSERT_EQUAL_UINT8(132, olds.end);
  TEST_ASSERT_EQUAL_UINT8(10, olds.x);
  TEST_ASSERT_EQUAL_UINT8(132, news.top);
  TEST_ASSERT_EQUAL_UINT8(240, news.end);
  TEST_ASSERT_EQUAL_UINT8(77, news.code);
  TEST_ASSERT_EQUAL_UINT8(50, news.x);
}

static void test_draw_order_slot_23_first(void) {
  b.beginFrame();
  uint8_t n = b.finish(out);
  TEST_ASSERT_EQUAL_UINT8(SLOTS, n);
  TEST_ASSERT_EQUAL_UINT8(23, out[0].slot);
  TEST_ASSERT_EQUAL_UINT8(0, out[n - 1].slot);
}

static void test_overflow_keeps_current_content(void) {
  b.beginFrame();
  // code byte of slot 0 changes on 100 lines -> more retires than fit
  for (int line = 20; line < 120; line++) b.write(1, (uint8_t)line, (uint8_t)line);
  uint8_t n = b.finish(out);
  TEST_ASSERT_TRUE(n <= MAX_SPRITES);
  bool has_last = false;
  for (int i = 0; i < n; i++)
    if (out[i].slot == 0 && out[i].code == 119 && out[i].end == 240) has_last = true;
  TEST_ASSERT_TRUE(has_last);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_midscreen_rewrite_splits_slot);
  RUN_TEST(test_draw_order_slot_23_first);
  RUN_TEST(test_overflow_keeps_current_content);
  return UNITY_END();
}
