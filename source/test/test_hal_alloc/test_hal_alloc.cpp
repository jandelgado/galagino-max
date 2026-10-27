#include <unity.h>
#include <stdlib.h>
#include "../../src/hal/hal.h"

void setUp(void) {}
void tearDown(void) {}

static void test_alloc_spare_returns_writable_memory(void) {
  uint8_t *p = (uint8_t *)hal::allocSpare(64);
  TEST_ASSERT_NOT_NULL(p);
  p[0] = 1;
  p[63] = 2;
  TEST_ASSERT_EQUAL_UINT8(1, p[0]);
  TEST_ASSERT_EQUAL_UINT8(2, p[63]);
  free(p);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_alloc_spare_returns_writable_memory);
  return UNITY_END();
}
