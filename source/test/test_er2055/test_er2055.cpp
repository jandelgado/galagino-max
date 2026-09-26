#include <unity.h>

#include "../../src/emulation/er2055.h"

// control bytes as the game writes them: DB3 = CS1, DB2 = C2, DB1 = !C1, DB0 = CK
static const uint8_t SEL = 0x08;
static const uint8_t MODE_READ = SEL;                // C1 = 1, C2 = 0
static const uint8_t MODE_WRITE = SEL | 0x02;        // C1 = 0, C2 = 0
static const uint8_t MODE_ERASE = SEL | 0x02 | 0x04; // C1 = 0, C2 = 1

static uint8_t read_at(Er2055 &e, uint8_t a) {
  e.latch(a, 0);
  e.control(MODE_READ | 1);
  e.control(MODE_READ); // falling edge loads the bus
  return e.read();
}

static void pulse(Er2055 &e, uint8_t mode) {
  e.control(mode | 1);
  e.control(mode);
}

void setUp(void) {}
void tearDown(void) {}

static void test_power_up_erased(void) {
  Er2055 e;
  TEST_ASSERT_EQUAL_HEX8(0xff, read_at(e, 5));
}

static void test_write_after_erase(void) {
  Er2055 e;
  e.reset();
  e.latch(7, 0);
  pulse(e, MODE_ERASE);
  e.latch(7, 0x5a);
  pulse(e, MODE_WRITE);
  TEST_ASSERT_EQUAL_HEX8(0x5a, read_at(e, 7));
}

static void test_write_without_erase_ands(void) {
  Er2055 e;
  e.latch(3, 0x0f);
  pulse(e, MODE_WRITE);
  e.latch(3, 0x3c);
  pulse(e, MODE_WRITE);
  TEST_ASSERT_EQUAL_HEX8(0x0c, read_at(e, 3));
}

static void test_deselected_ignored(void) {
  Er2055 e;
  e.latch(1, 0x00);
  pulse(e, 0x02); // write mode, CS1 low
  TEST_ASSERT_EQUAL_HEX8(0xff, read_at(e, 1));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_power_up_erased);
  RUN_TEST(test_write_after_erase);
  RUN_TEST(test_write_without_erase_ands);
  RUN_TEST(test_deselected_ignored);
  return UNITY_END();
}
