#include <unity.h>

#include "../../src/machines/system1/system1_pio.h"

static Z80Pio pio;

// offset bit 0 = port B, bit 1 = control
static const uint8_t DATA_A = 0;
static const uint8_t DATA_B = 1;
static const uint8_t CTRL_A = 2;
static const uint8_t CTRL_B = 3;
static const uint8_t MODE_OUTPUT = 0x0f;
static const uint8_t MODE_BIT = 0xcf;

void setUp(void) { pio.reset(); }
void tearDown(void) {}

static void test_reset_idle(void) {
  TEST_ASSERT_FALSE(pio.takeNmi());
  TEST_ASSERT_EQUAL_HEX8(0x00, pio.portB());
}

// MAME set_mode(MODE_OUTPUT) asserts ARDY: one rising edge
static void test_output_mode_raises_ready(void) {
  pio.write(CTRL_A, MODE_OUTPUT);
  TEST_ASSERT_TRUE(pio.takeNmi());
  TEST_ASSERT_FALSE(pio.takeNmi());
}

static void test_command_nmi_and_ack(void) {
  pio.write(CTRL_A, MODE_OUTPUT);
  pio.takeNmi();
  pio.write(DATA_A, 0x42);
  TEST_ASSERT_TRUE(pio.takeNmi());
  TEST_ASSERT_FALSE(pio.takeNmi());
  TEST_ASSERT_EQUAL_HEX8(0x42, pio.ackA());
}

// data_write drops and raises RDY: every write is an edge, latest value wins
static void test_two_writes_before_ack(void) {
  pio.write(CTRL_A, MODE_OUTPUT);
  pio.takeNmi();
  pio.write(DATA_A, 0x01);
  TEST_ASSERT_TRUE(pio.takeNmi());
  pio.write(DATA_A, 0x02);
  TEST_ASSERT_TRUE(pio.takeNmi());
  TEST_ASSERT_EQUAL_HEX8(0x02, pio.ackA());
  pio.write(DATA_A, 0x03);
  TEST_ASSERT_TRUE(pio.takeNmi());
}

// input mode (reset): data writes latch without RDY
static void test_input_mode_no_nmi(void) {
  pio.write(DATA_A, 0x55);
  TEST_ASSERT_FALSE(pio.takeNmi());
}

// port B bit control mode: next control word is the I/O mask, not a mode
static void test_port_b_video_mode(void) {
  pio.write(CTRL_B, MODE_BIT);
  pio.write(CTRL_B, 0x00);
  pio.write(DATA_B, 0x10);
  TEST_ASSERT_EQUAL_HEX8(0x10, pio.portB());
  TEST_ASSERT_FALSE(pio.takeNmi());
}

static void test_port_b_output_no_nmi(void) {
  pio.write(CTRL_B, MODE_OUTPUT);
  pio.write(DATA_B, 0x16);
  TEST_ASSERT_EQUAL_HEX8(0x16, pio.portB());
  TEST_ASSERT_FALSE(pio.takeNmi());
}

// interrupt control word with bit 4: next word is the mask
static void test_interrupt_mask_word_skipped(void) {
  pio.write(CTRL_A, 0x17);
  pio.write(CTRL_A, MODE_OUTPUT);  // consumed as mask: no mode change
  TEST_ASSERT_FALSE(pio.takeNmi());
  pio.write(CTRL_A, MODE_OUTPUT);
  TEST_ASSERT_TRUE(pio.takeNmi());
}

static void test_read_data_returns_latch(void) {
  pio.write(CTRL_B, MODE_OUTPUT);
  pio.write(DATA_B, 0x81);
  TEST_ASSERT_EQUAL_HEX8(0x81, pio.read(DATA_B));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_reset_idle);
  RUN_TEST(test_output_mode_raises_ready);
  RUN_TEST(test_command_nmi_and_ack);
  RUN_TEST(test_two_writes_before_ack);
  RUN_TEST(test_input_mode_no_nmi);
  RUN_TEST(test_port_b_video_mode);
  RUN_TEST(test_port_b_output_no_nmi);
  RUN_TEST(test_interrupt_mask_word_skipped);
  RUN_TEST(test_read_data_returns_latch);
  return UNITY_END();
}
