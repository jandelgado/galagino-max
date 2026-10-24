// Ay8910 envelope as MAME ay8910.cpp: shapes (continue/attack/alternate/
// hold), one step per 2 x period chip clocks / 8, and a tone above Nyquist
// (period 0) that plays the envelope level instead of aliasing.
#include <stdlib.h>
#include <unity.h>

#include "../../src/emulation/ay8910.cpp"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t INC = 9;
static const uint8_t GAIN = 2;
static const int N = 24000;   // one second
static int16_t out[N];

// voice A: tone period 0, tone on, noise off, volume from the envelope
static void play(uint8_t shape, uint16_t envPeriod) {
  uint8_t regs[16] = {0};
  regs[7] = 0x3e;
  regs[8] = 0x10;
  regs[11] = envPeriod & 0xff;
  regs[12] = envPeriod >> 8;
  regs[13] = shape;
  Ay8910 ay(INC, GAIN);
  for (int i = 0; i < N; i += AUDIO_CHUNK) {
    ay.render(regs, out + i, AUDIO_CHUNK);
  }
}

static int level(int i) { return out[i] / GAIN; }

// samples per envelope step: 2 * period / INC
static int stepAt(int step, uint16_t envPeriod) { return step * 2 * envPeriod / INC; }

// shape 0 (\___): 15 down to 0, then holds 0
static void test_decay_holds_zero(void) {
  play(0x00, 900);
  TEST_ASSERT_EQUAL(15, level(0));
  TEST_ASSERT_EQUAL(0, level(stepAt(17, 900)));
  TEST_ASSERT_EQUAL(0, level(N - 1));
}

// shape 4 (/___): 0 up to 15, then drops to 0 and holds
static void test_attack_drops_to_zero(void) {
  play(0x04, 900);
  TEST_ASSERT_EQUAL(0, level(0));
  TEST_ASSERT_EQUAL(15, level(stepAt(15, 900) + 10));
  TEST_ASSERT_EQUAL(0, level(N - 1));
}

// shape 14 (/\/\): keeps going up and down
static void test_triangle_continues(void) {
  play(0x0e, 90);
  int lo = 15, hi = 0;
  for (int i = N / 2; i < N; i++) {
    lo = level(i) < lo ? level(i) : lo;
    hi = level(i) > hi ? level(i) : hi;
  }
  TEST_ASSERT_EQUAL(0, lo);
  TEST_ASSERT_EQUAL(15, hi);
}

// shape 8 (\\\\): sawtooth, 16 steps of 2 * period clocks / 8
static void test_saw_rate(void) {
  play(0x08, 450);   // 100 samples per step, 1600 per cycle
  int wraps = 0;
  for (int i = 1; i < N; i++) {
    wraps += level(i) > level(i - 1);
  }
  TEST_ASSERT_INT_WITHIN(1, N / 1600, wraps);
}

// shape 11 (\```): down to 0, then holds 15
static void test_decay_holds_high(void) {
  play(0x0b, 900);
  TEST_ASSERT_EQUAL(15, level(N - 1));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_decay_holds_zero);
  RUN_TEST(test_attack_drops_to_zero);
  RUN_TEST(test_triangle_continues);
  RUN_TEST(test_saw_rate);
  RUN_TEST(test_decay_holds_high);
  return UNITY_END();
}
