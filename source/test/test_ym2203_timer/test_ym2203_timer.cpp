#include <unity.h>

#include "../../src/emulation/ym2203.cpp"
#include "../../src/emulation/ym2203fm.cpp"
#include "../../src/emulation/ay8910.cpp"

void setUp(void) {}
void tearDown(void) {}

static void wr(Ym2203 &y, uint8_t reg, uint8_t v) {
  y.writeAddress(reg);
  y.writeData(v);
}

static void timerBNb(Ym2203 &y) {
  wr(y, 0x26, 0xFA);
  wr(y, 0x27, 0x39);
  wr(y, 0x27, 0x3b);
}

void test_timer_b_period(void) {
  Ym2203 y(1, 1);
  timerBNb(y);
  TEST_ASSERT_EQUAL_UINT32(6912, y.cyclesToTimerB());
  y.tick(6911);
  TEST_ASSERT_EQUAL_UINT8(0, y.readStatus() & 2);
  y.tick(1);
  TEST_ASSERT_EQUAL_UINT8(2, y.readStatus() & 2);
  TEST_ASSERT_EQUAL_UINT8(0, y.readStatus() & 0x80);
}

void test_reg27_clears_flag(void) {
  Ym2203 y(1, 1);
  timerBNb(y);
  y.tick(6912);
  TEST_ASSERT_EQUAL_UINT8(2, y.readStatus() & 2);
  wr(y, 0x27, 0x39);
  TEST_ASSERT_EQUAL_UINT8(0, y.readStatus() & 2);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, y.cyclesToTimerB());
}

void test_cycles_to_b(void) {
  Ym2203 y(1, 1);
  timerBNb(y);
  y.tick(1000);
  TEST_ASSERT_EQUAL_UINT32(5912, y.cyclesToTimerB());
}

void test_b_reloads(void) {
  Ym2203 y(1, 1);
  timerBNb(y);
  y.tick(6912 + 100);
  TEST_ASSERT_EQUAL_UINT32(6812, y.cyclesToTimerB());
}

void test_a_not_set_without_enable(void) {
  Ym2203 y(1, 1);
  wr(y, 0x24, 0);
  wr(y, 0x25, 0);
  wr(y, 0x27, 0x01); // load A, flag disabled
  y.tick(100000);
  TEST_ASSERT_EQUAL_UINT8(0, y.readStatus() & 1);
}

void test_a_period(void) {
  Ym2203 y(1, 1);
  wr(y, 0x24, 0xFF); // NA = 1023 -> 1
  wr(y, 0x25, 0x03);
  wr(y, 0x27, 0x15); // load A, enable A, reset
  y.tick(71);
  TEST_ASSERT_EQUAL_UINT8(0, y.readStatus() & 1);
  y.tick(1);
  TEST_ASSERT_EQUAL_UINT8(1, y.readStatus() & 1);
}

void test_prescaler(void) {
  Ym2203 y(1, 1);
  y.writeAddress(0x2e); // /3
  timerBNb(y);
  TEST_ASSERT_EQUAL_UINT32(3456, y.cyclesToTimerB());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_timer_b_period);
  RUN_TEST(test_reg27_clears_flag);
  RUN_TEST(test_cycles_to_b);
  RUN_TEST(test_b_reloads);
  RUN_TEST(test_a_not_set_without_enable);
  RUN_TEST(test_a_period);
  RUN_TEST(test_prescaler);
  return UNITY_END();
}
