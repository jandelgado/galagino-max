#include <unity.h>
#include "../../src/machines/williams/pia6821.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t ORA = 0, CRA = 1, ORB = 2, CRB = 3;
static const uint8_t CR_OR = 0x04;           // select data register, not DDR
static const uint8_t CR_IRQ1_EN = 0x01, CR_IRQ1_RISE = 0x02;

void test_ddr_then_output(void) {
  Pia6821 p;
  p.reset();
  p.write(ORA, 0xff);                 // CRA b2 = 0: DDR A
  p.write(CRA, CR_OR);
  TEST_ASSERT_EQUAL(Pia6821::PORT_A, p.write(ORA, 0x5a));
  TEST_ASSERT_EQUAL_HEX8(0x5a, p.outA());
}

void test_input_pins_read_through(void) {
  Pia6821 p;
  p.reset();
  p.write(CRB, CR_OR);
  p.setInputs(0x00, 0x81);
  TEST_ASSERT_EQUAL_HEX8(0x81, p.read(ORB));
}

void test_cb1_rising_edge_irq_cleared_by_read(void) {
  Pia6821 p;
  p.reset();
  p.write(CRB, CR_OR | CR_IRQ1_EN | CR_IRQ1_RISE);
  p.cb1(false);
  TEST_ASSERT_FALSE(p.irq());
  p.cb1(true);
  TEST_ASSERT_TRUE(p.irq());
  TEST_ASSERT_EQUAL_HEX8(0x80, p.read(CRB) & 0x80);
  p.read(ORB);
  TEST_ASSERT_FALSE(p.irq());
}

void test_falling_edge_and_disabled_irq(void) {
  Pia6821 p;
  p.reset();
  p.write(CRA, CR_OR);                // falling edge, IRQ disabled
  p.ca1(true);
  p.ca1(false);
  TEST_ASSERT_FALSE(p.irq());         // flag set but not enabled
  TEST_ASSERT_EQUAL_HEX8(0x80, p.read(CRA) & 0x80);
  p.write(CRA, CR_OR | CR_IRQ1_EN);   // enabling raises the pending flag
  TEST_ASSERT_TRUE(p.irq());
}

void test_cb2_manual_output(void) {
  Pia6821 p;
  p.reset();
  TEST_ASSERT_FALSE(p.cb2());
  p.write(CRB, 0x38 | CR_OR);         // b5 output, b4 manual, b3 level 1
  TEST_ASSERT_TRUE(p.cb2());
  p.write(CRB, 0x30 | CR_OR);
  TEST_ASSERT_FALSE(p.cb2());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ddr_then_output);
  RUN_TEST(test_input_pins_read_through);
  RUN_TEST(test_cb1_rising_edge_irq_cleared_by_read);
  RUN_TEST(test_falling_edge_and_disabled_irq);
  RUN_TEST(test_cb2_manual_output);
  return UNITY_END();
}
