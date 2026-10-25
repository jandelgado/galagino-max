#include <unity.h>
#include "../../src/hal/hal.h"

void setUp(void) {}
void tearDown(void) {}

static void test_micros_wraps_at_32_bit(void) {
  hal::host::setMicros(0xFFFFFFF0u);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFF0u, hal::micros());
  hal::host::setMicros(0x100000010ull);
  TEST_ASSERT_EQUAL_UINT32(0x10u, hal::micros());
}

static void test_unsigned_difference_across_wrap(void) {
  hal::host::setMicros(0xFFFFFFF0u);
  const uint32_t t0 = hal::micros();
  hal::host::setMicros(0x100000010ull);
  TEST_ASSERT_EQUAL_UINT32(0x20u, (uint32_t)(hal::micros() - t0));
}

static void test_millis_from_64_bit_time(void) {
  hal::host::setMicros(0x100000010ull);
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(0x100000010ull / 1000), hal::millis());
}

static void test_gpio_host_levels(void) {
  hal::pinInputPullup(4);
  TEST_ASSERT_TRUE(hal::pinRead(4));      // pull-up reads high
  hal::host::setPinLevel(4, false);
  TEST_ASSERT_FALSE(hal::pinRead(4));
  hal::pinOutput(5, true);
  TEST_ASSERT_TRUE(hal::pinRead(5));
  hal::pinWrite(5, false);
  TEST_ASSERT_FALSE(hal::pinRead(5));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_micros_wraps_at_32_bit);
  RUN_TEST(test_unsigned_difference_across_wrap);
  RUN_TEST(test_millis_from_64_bit_time);
  RUN_TEST(test_gpio_host_levels);
  return UNITY_END();
}
