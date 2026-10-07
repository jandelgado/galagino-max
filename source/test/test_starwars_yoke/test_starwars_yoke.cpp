#include <unity.h>

#include "../../src/machines/starwars/starwars_yoke.h"

void setUp(void) {}
void tearDown(void) {}

static void hold(YokeAxis &y, int8_t dir, int frames) {
  for (int i = 0; i < frames; i++) {
    y.step(dir);
  }
}

static void test_holds_after_release(void) {
  YokeAxis y;
  hold(y, 1, 5);
  const uint8_t pos = y.pos;
  TEST_ASSERT_GREATER_THAN_UINT8(YokeAxis::CENTER, pos);

  hold(y, 0, 60);
  TEST_ASSERT_EQUAL_HEX8(pos, y.pos);
}

static void test_clamps_at_ends(void) {
  YokeAxis y;
  hold(y, 1, 100);
  TEST_ASSERT_EQUAL_HEX8(0xff, y.pos);

  hold(y, 0, 1);
  hold(y, -1, 100); // stops at center detent
  hold(y, 0, 1);
  hold(y, -1, 100);
  TEST_ASSERT_EQUAL_HEX8(0x00, y.pos);
}

// reversing stops at center until the stick is released
static void test_center_detent(void) {
  YokeAxis y;
  hold(y, 1, 10);
  hold(y, 0, 1);

  hold(y, -1, 100);
  TEST_ASSERT_EQUAL_HEX8(YokeAxis::CENTER, y.pos);

  hold(y, 0, 1);
  hold(y, -1, 1);
  TEST_ASSERT_LESS_THAN_UINT8(YokeAxis::CENTER, y.pos);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_holds_after_release);
  RUN_TEST(test_clamps_at_ends);
  RUN_TEST(test_center_detent);
  return UNITY_END();
}
