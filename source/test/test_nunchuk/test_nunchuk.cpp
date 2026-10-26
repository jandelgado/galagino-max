#include <unity.h>

// Nunchuk against the host I2C bus: unencrypted init, 6 byte report,
// next report requested after each read, 100 ms poll throttle.
#define NUNCHUCK_INPUT
#define NUNCHUCK_SDA 33
#define NUNCHUCK_SCL 32
#define NUNCHUCK_MOVE_THRESHOLD 30
#include "../../src/input/nunchuk.cpp"

static constexpr uint8_t NUNCHUK_ADDR = 0x52;
static constexpr uint8_t CENTER = 127;
static constexpr uint8_t NO_BUTTONS = 0x03;  // Z, C released (active low)

static Nunchuk pad;

void setUp(void) {
  hal::host::i2cReset();
  hal::host::setMicros(0);
  pad = Nunchuk();
}
void tearDown(void) {}

static void attach(uint8_t x, uint8_t y, uint8_t buttons) {
  const uint8_t report[6] = {x, y, 0x80, 0x80, 0x80, buttons};
  hal::host::i2cAttach(0, NUNCHUK_ADDR, report, 6);
}

static uint16_t poll() {
  hal::host::advanceMicros(100000);
  return pad.getInput();
}

static void test_setup_selects_unencrypted_reports(void) {
  attach(CENTER, CENTER, NO_BUTTONS);
  pad.setup();
  const uint16_t expected[2] = {0xf055, 0xfb00};
  TEST_ASSERT_EQUAL_UINT32(2, hal::host::i2cWriteLogLen());
  TEST_ASSERT_EQUAL_HEX16_ARRAY(expected, hal::host::i2cWriteLog(), 2);
  TEST_ASSERT_EQUAL_UINT32(3, hal::host::i2cWrites(0, NUNCHUK_ADDR));  // + first request
}

static void test_centered_stick_no_buttons(void) {
  attach(CENTER, CENTER, NO_BUTTONS);
  pad.setup();
  TEST_ASSERT_EQUAL_HEX(0, poll());
}

static void test_stick_beyond_threshold(void) {
  attach(CENTER - 31, CENTER + 31, NO_BUTTONS);
  pad.setup();
  TEST_ASSERT_EQUAL_HEX(BUTTON_LEFT | BUTTON_UP, poll());

  attach(CENTER + 31, CENTER - 31, NO_BUTTONS);
  TEST_ASSERT_EQUAL_HEX(BUTTON_RIGHT | BUTTON_DOWN, poll());

  attach(CENTER - 30, CENTER + 30, NO_BUTTONS);  // inside dead zone
  TEST_ASSERT_EQUAL_HEX(0, poll());
}

static void test_z_is_fire_c_is_extra(void) {
  attach(CENTER, CENTER, 0x02);  // Z pressed
  pad.setup();
  TEST_ASSERT_EQUAL_HEX(BUTTON_FIRE, poll());

  attach(CENTER, CENTER, 0x01);  // C pressed
  TEST_ASSERT_EQUAL_HEX(BUTTON_EXTRA, poll());
}

static void test_each_read_requests_the_next_report(void) {
  attach(CENTER, CENTER, NO_BUTTONS);
  pad.setup();
  const uint32_t writes = hal::host::i2cWrites(0, NUNCHUK_ADDR);
  poll();
  poll();
  TEST_ASSERT_EQUAL_UINT32(2, hal::host::i2cReads(0, NUNCHUK_ADDR));
  TEST_ASSERT_EQUAL_UINT32(writes + 2, hal::host::i2cWrites(0, NUNCHUK_ADDR));
}

static void test_input_is_throttled_to_100_ms(void) {
  attach(CENTER, CENTER, 0x02);
  pad.setup();
  TEST_ASSERT_EQUAL_HEX(BUTTON_FIRE, poll());
  attach(CENTER, CENTER, NO_BUTTONS);
  hal::host::advanceMicros(50000);
  TEST_ASSERT_EQUAL_HEX(BUTTON_FIRE, pad.getInput());  // cached
  TEST_ASSERT_EQUAL_UINT32(1, hal::host::i2cReads(0, NUNCHUK_ADDR));
  TEST_ASSERT_EQUAL_HEX(0, poll());
}

static void test_disable_stops_reads(void) {
  attach(CENTER, CENTER, NO_BUTTONS);
  pad.setup();
  pad.disable();
  poll();
  TEST_ASSERT_EQUAL_UINT32(0, hal::host::i2cReads(0, NUNCHUK_ADDR));
}

static void test_missing_nunchuk(void) {
  pad.setup();
  TEST_ASSERT_FALSE(pad.connected());
  TEST_ASSERT_EQUAL_HEX(0, poll());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_setup_selects_unencrypted_reports);
  RUN_TEST(test_centered_stick_no_buttons);
  RUN_TEST(test_stick_beyond_threshold);
  RUN_TEST(test_z_is_fire_c_is_extra);
  RUN_TEST(test_each_read_requests_the_next_report);
  RUN_TEST(test_input_is_throttled_to_100_ms);
  RUN_TEST(test_disable_stops_reads);
  RUN_TEST(test_missing_nunchuk);
  return UNITY_END();
}
