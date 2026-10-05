#include <unity.h>

#include "../../src/emulation/discrete.cpp"

void setUp(void) {}
void tearDown(void) {}

// from lfsr 0 the XNOR feeds seven 1s, then bit 6 is set and a 0 follows
static void test_noise_sequence(void) {
  discrete::Noise n;
  n.reset();
  for (int tick = 0; tick < 7; tick++) {
    n.step(0);
    TEST_ASSERT_FALSE(n.ticked());
    TEST_ASSERT_EQUAL_FLOAT(0.5f, n.step(0));
    TEST_ASSERT_TRUE(n.ticked());
  }
  n.step(0);
  TEST_ASSERT_EQUAL_FLOAT(-0.5f, n.step(0));
}

static void test_noise_reset_count(void) {
  discrete::Noise n;
  n.reset();
  for (int i = 0; i < 20; i++) {
    n.step(0);
  }
  n.step(1); // new reset count clears the register
  TEST_ASSERT_EQUAL_FLOAT(0.5f, n.step(1));
  TEST_ASSERT_EQUAL_HEX16(1, n.lfsr);
}

static void test_lowpass_settles(void) {
  discrete::LowPass lp;
  lp.set(560.0f);
  float y = 0;
  for (int i = 0; i < 2400; i++) {
    y = lp.step(1.0f);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, y);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_noise_sequence);
  RUN_TEST(test_noise_reset_count);
  RUN_TEST(test_lowpass_settles);
  return UNITY_END();
}
