#include <unity.h>

// input.h needs Arduino.h; same values as there
#define BUTTON_LEFT 0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP 0x04
#define BUTTON_DOWN 0x08
#include "../../src/machines/bzone/bzone_treads.h"

void setUp(void) {}
void tearDown(void) {}

static void test_bzone_treads(void) {
  TEST_ASSERT_EQUAL_HEX8(0x00, bzone_treads(0));
  TEST_ASSERT_EQUAL_HEX8(BZ_LEFT_FWD | BZ_RIGHT_FWD, bzone_treads(BUTTON_UP));
  TEST_ASSERT_EQUAL_HEX8(BZ_LEFT_BACK | BZ_RIGHT_BACK, bzone_treads(BUTTON_DOWN));
  TEST_ASSERT_EQUAL_HEX8(BZ_LEFT_BACK | BZ_RIGHT_FWD, bzone_treads(BUTTON_LEFT));
  TEST_ASSERT_EQUAL_HEX8(BZ_LEFT_FWD | BZ_RIGHT_BACK, bzone_treads(BUTTON_RIGHT));
  TEST_ASSERT_EQUAL_HEX8(BZ_RIGHT_FWD, bzone_treads(BUTTON_UP | BUTTON_LEFT));
  TEST_ASSERT_EQUAL_HEX8(BZ_LEFT_FWD, bzone_treads(BUTTON_UP | BUTTON_RIGHT));
  TEST_ASSERT_EQUAL_HEX8(BZ_LEFT_BACK, bzone_treads(BUTTON_DOWN | BUTTON_LEFT));
  TEST_ASSERT_EQUAL_HEX8(BZ_RIGHT_BACK, bzone_treads(BUTTON_DOWN | BUTTON_RIGHT));
  // contradicting directions cancel
  TEST_ASSERT_EQUAL_HEX8(0x00, bzone_treads(BUTTON_LEFT | BUTTON_RIGHT));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_bzone_treads);
  return UNITY_END();
}
