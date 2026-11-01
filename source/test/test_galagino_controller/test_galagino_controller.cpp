#include <unity.h>

// ControllerI2C (galagino I2C controller) against the host I2C bus: probe at
// enable, 33 ms update throttle, little endian 16 bit button word.
#define GALAGINO_CONTROLLER
#define GALAGINO_CONTROLLER_ADDR 0x09
#define GALAGINO_CONTROLLER_SDA 27
#define GALAGINO_CONTROLLER_SCL 22
#include "../../src/input/galagino_controller.cpp"

static ControllerI2C pad;

void setUp(void) {
  hal::host::i2cReset();
  hal::host::setMicros(0);
  pad = ControllerI2C();
}
void tearDown(void) {}

static void attach(uint8_t lo, uint8_t hi) {
  const uint8_t word[2] = {lo, hi};
  hal::host::i2cAttach(0, 0x09, word, 2);
}

static void test_setup_enables_a_present_controller(void) {
  attach(0x34, 0x12);
  pad.setup();
  hal::host::advanceMicros(40000);
  TEST_ASSERT_EQUAL_HEX(0x1234, pad.getInput());
}

static void test_input_is_throttled_to_33_ms(void) {
  attach(0x01, 0x00);
  pad.setup();
  hal::host::advanceMicros(40000);
  TEST_ASSERT_EQUAL_HEX(0x01, pad.getInput());
  const uint32_t reads = hal::host::i2cReads(0, 0x09);
  attach(0x02, 0x00);
  hal::host::advanceMicros(20000);
  TEST_ASSERT_EQUAL_HEX(0x01, pad.getInput());              // cached
  TEST_ASSERT_EQUAL_UINT32(reads, hal::host::i2cReads(0, 0x09));
  hal::host::advanceMicros(20000);
  TEST_ASSERT_EQUAL_HEX(0x02, pad.getInput());              // 40 ms later: read again
}

static void test_missing_controller_stays_dead(void) {
  pad.setup();  // nothing at 0x09
  hal::host::advanceMicros(100000);
  TEST_ASSERT_EQUAL_HEX(0, pad.getInput());
  TEST_ASSERT_EQUAL_UINT32(0, hal::host::i2cReads(0, 0x09));
}

static void test_disable_stops_reads(void) {
  attach(0x10, 0x00);
  pad.setup();
  pad.disable();
  hal::host::advanceMicros(100000);
  TEST_ASSERT_EQUAL_HEX(0, pad.getInput());
  TEST_ASSERT_EQUAL_UINT32(0, hal::host::i2cReads(0, 0x09));
}

// a controller answering one byte only: low byte valid, high byte 0
static void test_short_read_keeps_low_byte(void) {
  const uint8_t one[1] = {0x55};
  hal::host::i2cAttach(0, 0x09, one, 1);
  pad.setup();
  hal::host::advanceMicros(40000);
  TEST_ASSERT_EQUAL_HEX(0x55, pad.getInput());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_setup_enables_a_present_controller);
  RUN_TEST(test_input_is_throttled_to_33_ms);
  RUN_TEST(test_missing_controller_stays_dead);
  RUN_TEST(test_disable_stops_reads);
  RUN_TEST(test_short_read_keeps_low_byte);
  return UNITY_END();
}
