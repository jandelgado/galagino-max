// Z80 CTC model against MAME z80ctc.cpp semantics, values from Tron's
// CTC programming (vector 0x08, ch1 timer, ch3 counter, ch0 armed/reset).
#include <unity.h>

#include "../../src/emulation/z80ctc.h"

void setUp(void) {}
void tearDown(void) {}

static void test_vector_and_counter_irq(void) {
  Z80Ctc c;
  c.reset();
  c.write(0, 0x08);                   // vector base
  c.write(3, 0xc7); c.write(3, 0x01); // ch3 counter, tc 1, IRQ
  TEST_ASSERT_FALSE(c.irq());
  c.trigger(3);
  TEST_ASSERT_TRUE(c.irq());
  TEST_ASSERT_EQUAL_HEX8(0x0e, c.ack());
  TEST_ASSERT_FALSE(c.irq());
}

static void test_timer_period_and_reset_cancels(void) {
  Z80Ctc c;
  c.reset();
  c.write(0, 0x08);
  c.write(0, 0x87); c.write(0, 0xbb); // prescale 16, tc 187
  c.tick(16 * 187 - 1);
  TEST_ASSERT_FALSE(c.irq());
  c.tick(1);
  TEST_ASSERT_TRUE(c.irq());
  TEST_ASSERT_EQUAL_HEX8(0x08, c.ack());
  c.write(0, 0x03);                   // channel reset
  c.tick(16 * 187 * 2);
  TEST_ASSERT_FALSE(c.irq());
}

static void test_priority(void) {
  Z80Ctc c;
  c.reset();
  c.write(0, 0x08);
  c.write(1, 0xa7); c.write(1, 0x01); // ch1 timer, 256 cycles
  c.write(3, 0xc7); c.write(3, 0x01);
  c.tick(256);
  c.trigger(3);
  TEST_ASSERT_EQUAL_HEX8(0x0a, c.ack()); // ch1 before ch3
  TEST_ASSERT_TRUE(c.irq());
  TEST_ASSERT_EQUAL_HEX8(0x0e, c.ack());
  TEST_ASSERT_FALSE(c.irq());
}

static void test_tc_zero_is_256_and_zc(void) {
  Z80Ctc c;
  c.reset();
  c.write(0, 0x45); c.write(0, 0x00); // counter, no IRQ, tc 256
  for (int i = 0; i < 255; i++) c.trigger(0);
  TEST_ASSERT_FALSE(c.zc(0));
  c.trigger(0);
  TEST_ASSERT_TRUE(c.zc(0));
  TEST_ASSERT_FALSE(c.irq());
}

static void test_irq_disable_drops_pending(void) {
  Z80Ctc c;
  c.reset();
  c.write(3, 0xc7); c.write(3, 0x01);
  c.trigger(3);
  c.write(3, 0x47); c.write(3, 0x01); // same mode, IRQ off
  TEST_ASSERT_FALSE(c.irq());
}

static void test_timer_waits_for_trigger(void) {
  Z80Ctc c;
  c.reset();
  c.write(2, 0x8f); c.write(2, 0x01); // timer, wait for trigger, reset, tc 1
  c.tick(64);
  TEST_ASSERT_FALSE(c.irq());
  c.trigger(2);
  c.tick(16);
  TEST_ASSERT_TRUE(c.irq());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_vector_and_counter_irq);
  RUN_TEST(test_timer_period_and_reset_cancels);
  RUN_TEST(test_priority);
  RUN_TEST(test_tc_zero_is_256_and_zc);
  RUN_TEST(test_irq_disable_drops_pending);
  RUN_TEST(test_timer_waits_for_trigger);
  return UNITY_END();
}
