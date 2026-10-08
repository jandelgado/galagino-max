#include <unity.h>

#include "../../src/machines/mpatrol/mpatrol_video.h"

using namespace mpatrol_video;

void setUp(void) {}
void tearDown(void) {}

static void test_protection(void) {
  TEST_ASSERT_EQUAL_UINT8(0, protection(0x00));
  TEST_ASSERT_EQUAL_UINT8(7, protection(0x7f));
  TEST_ASSERT_EQUAL_UINT8(0, protection(0x81)); // popcount 1 ^ 1
  TEST_ASSERT_EQUAL_UINT8(6, protection(0xff)); // 7 ^ 1
}

static void test_char_x(void) {
  TEST_ASSERT_EQUAL_UINT8(8, charX(0, 0x55, 0)); // static part ignores scroll
  TEST_ASSERT_EQUAL_UINT8(247, charX(239, 0x55, 191));
  TEST_ASSERT_EQUAL_UINT8(8, charX(0, 0, 192));
  TEST_ASSERT_EQUAL_UINT8(0xf8, charX(0, 0x10, 200)); // 8 - 16 wraps
}

static void test_char_y(void) {
  TEST_ASSERT_EQUAL_UINT8(6, charY(0));
  TEST_ASSERT_EQUAL_UINT8(1, charY(251));
}

static void test_bg(void) {
  TEST_ASSERT_EQUAL_UINT8(12, bgImageX(0, 0));
  TEST_ASSERT_EQUAL_UINT8(0, bgImageX(0, 12));
  TEST_ASSERT_EQUAL_UINT8(255, bgImageX(0, 13));
  TEST_ASSERT_EQUAL_INT(0, bgImageY(0, 6));
  TEST_ASSERT_EQUAL_INT(-1, bgImageY(0, 7));
  TEST_ASSERT_EQUAL_INT(255, bgImageY(249, 0));
  TEST_ASSERT_EQUAL_INT(-1, bgImageY(250, 0));
}

static void test_sprite_pos(void) {
  TEST_ASSERT_EQUAL_INT(235, spriteLine(0));
  TEST_ASSERT_EQUAL_INT(-20, spriteLine(255));
  TEST_ASSERT_EQUAL_INT(-7, spriteColumn(0));
  TEST_ASSERT_EQUAL_INT(248, spriteColumn(255));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_protection);
  RUN_TEST(test_char_x);
  RUN_TEST(test_char_y);
  RUN_TEST(test_bg);
  RUN_TEST(test_sprite_pos);
  return UNITY_END();
}
