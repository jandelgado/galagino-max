#include <unity.h>

// Es8311 codec setup against a register-file I2C device: the register writes
// of the Espressif driver galagino used before (lib/es8311), for 16 bit I2S
// slave input, MCLK = 256 x 24 kHz, volume 80.
#include "../../src/audio/codec_es8311.cpp"

static const uint8_t ADDR = 0x18;

void setUp(void) {
  hal::host::i2cReset();
  hal::host::i2cAttachRegisters(0, ADDR);
}
void tearDown(void) {}

static void test_init_writes_the_driver_sequence(void) {
  static const uint16_t expected[] = {
    0x001F, 0x0000, 0x0080,                          // reset, power on
    0x013F, 0x0600,                                  // clocks on, MCLK pin, SCLK polarity
    0x0200, 0x0310, 0x0410, 0x0500, 0x0603, 0x0700, 0x08FF,  // 6.144 MHz / 24 kHz dividers
    0x0080, 0x090C, 0x0A0C,                          // slave, 16 bit in/out
    0x0D01, 0x0E02, 0x1200, 0x1310, 0x1C6A, 0x3708,  // power up DAC path
    0x32CB,                                          // volume 80
  };
  const uint32_t n = sizeof(expected) / sizeof(expected[0]);

  TEST_ASSERT_TRUE(Es8311::init(0, ADDR, 24000, 80));
  TEST_ASSERT_EQUAL_UINT32(n, hal::host::i2cWriteLogLen());
  TEST_ASSERT_EQUAL_HEX16_ARRAY(expected, hal::host::i2cWriteLog(), n);
}

static void test_init_fails_without_the_chip(void) {
  hal::host::i2cReset();
  TEST_ASSERT_FALSE(Es8311::init(0, ADDR, 24000, 80));
}

static void test_init_rejects_a_rate_without_dividers(void) {
  TEST_ASSERT_FALSE(Es8311::init(0, ADDR, 22050, 80));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_init_writes_the_driver_sequence);
  RUN_TEST(test_init_fails_without_the_chip);
  RUN_TEST(test_init_rejects_a_rate_without_dividers);
  return UNITY_END();
}
