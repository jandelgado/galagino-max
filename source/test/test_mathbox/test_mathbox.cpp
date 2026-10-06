#include <unity.h>

#include "../../src/emulation/mathbox.cpp"

static Mathbox mb;

void setUp(void) { mb.reset(); }
void tearDown(void) {}

// 16 bit register load: low byte command, high byte command + 1
static void load(uint8_t cmd, int16_t v) {
  mb.go(cmd, (uint16_t)v & 0xff);
  mb.go(cmd + 1, (uint16_t)v >> 8);
}

static uint16_t result() { return mb.lo() | (mb.hi() << 8); }

// 0x1e: max(R2, R3) + 3/8 min(R2, R3)
static void test_distance(void) {
  load(0x04, 100);
  load(0x06, 40);
  mb.go(0x1e, 0);
  TEST_ASSERT_EQUAL_HEX16(115, result());
}

// 0x11: rotate (R4, R5) by cos R0 / sin R1, translate by (R2, R3), then
// the perspective divide with R6 = 0 (one step)
static void test_rotate(void) {
  load(0x00, 0x4000); // cos 0.5
  load(0x02, 0x2000); // sin 0.25
  load(0x04, 50);
  load(0x06, -20);
  load(0x08, 1000);
  mb.go(0x0a, (uint16_t)-600 & 0xff);
  mb.go(0x11, (uint16_t)-600 >> 8);
  TEST_ASSERT_EQUAL_HEX16(0x0000, result());
  mb.go(0x17, 0); // read back R7
  TEST_ASSERT_EQUAL_INT16(375, (int16_t)result());
  mb.go(0x19, 0); // read back R8
  TEST_ASSERT_EQUAL_INT16(-45, (int16_t)result());
}

// 0x14: R11:R10 / R7 with R6 + 1 steps
static void test_divide(void) {
  load(0x15, 300); // R7
  mb.go(0x0c, 15); // R6
  load(0x0d, 0);   // R10
  load(0x0f, 100); // R11
  mb.go(0x14, 0);
  TEST_ASSERT_EQUAL_HEX16(0x2aaa, result());
}

static void test_reset_clears(void) {
  load(0x04, 100);
  mb.reset();
  TEST_ASSERT_EQUAL_HEX16(0, result());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_distance);
  RUN_TEST(test_rotate);
  RUN_TEST(test_divide);
  RUN_TEST(test_reset_clears);
  return UNITY_END();
}
