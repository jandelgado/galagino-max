#include <unity.h>
#include "../../src/emulation/hc55516.h"

void setUp(void) {}
void tearDown(void) {}

// MAME process_bit by hand from reset (syl 0x3f, int 0): bit 1, rising
// edge: syl stays 0x3f, int += 0; falling edge: int -= max(2, 0x3f >> 6) = -2.
// output = (-2 << 6) | (((-2 & 0x3ff) ^ 0x200) >> 4) = -128 | 0x1f = -97
void test_one_bit_cycle(void) {
  Hc55516 c;
  c.reset();
  c.digit(true);
  c.clock(true);
  TEST_ASSERT_EQUAL_INT16(32, c.output());  // int 0: (0 ^ 0x200) >> 4
  c.clock(false);
  TEST_ASSERT_EQUAL_INT16(-97, c.output());
}

// same level twice is no edge
void test_no_edge_no_change(void) {
  Hc55516 c;
  c.reset();
  c.digit(true);
  c.clock(true);
  c.clock(false);
  const int16_t v = c.output();
  c.clock(false);
  TEST_ASSERT_EQUAL_INT16(v, c.output());
}

// a run of 0 bits drives the integrator up until it freezes at +0x180
void test_run_of_zeros_rises_and_saturates(void) {
  Hc55516 c;
  c.reset();
  c.digit(false);
  int16_t last = c.output();
  bool rose = false;
  for (int i = 0; i < 2000; i++) {
    c.clock(true);
    c.clock(false);
    rose |= c.output() > last;
    last = c.output();
  }
  TEST_ASSERT_TRUE(rose);
  TEST_ASSERT_TRUE(last >= (0x180 << 6));  // frozen at int >= 0x180
  TEST_ASSERT_TRUE(last < (0x200 << 6));
}

// alternating bits (idle pattern) stay near zero
void test_alternating_stays_small(void) {
  Hc55516 c;
  c.reset();
  for (int i = 0; i < 2000; i++) {
    c.digit(i & 1);
    c.clock(true);
    c.clock(false);
  }
  TEST_ASSERT_INT16_WITHIN(0x40 << 6, 0, c.output());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_one_bit_cycle);
  RUN_TEST(test_no_edge_no_change);
  RUN_TEST(test_run_of_zeros_rises_and_saturates);
  RUN_TEST(test_alternating_stays_small);
  return UNITY_END();
}
