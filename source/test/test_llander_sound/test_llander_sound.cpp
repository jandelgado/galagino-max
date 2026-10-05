#include <string.h>
#include <unity.h>

#include "../../src/emulation/discrete.cpp"
#include "../../src/emulation/llander_sound.cpp"

static LlanderSound snd;
static int16_t out[480];
static uint8_t regs[4];

void setUp(void) {
  snd.reset();
  memset(regs, 0, sizeof(regs));
}
void tearDown(void) {}

static void test_silent(void) {
  snd.render(regs, out, 480);
  for (int i = 0; i < 480; i++) {
    TEST_ASSERT_EQUAL(0, out[i]);
  }
}

// 3 kHz tone: 8 sample period, symmetric
static void test_tone_3k(void) {
  regs[LL_SND_CTRL] = LL_TONE_3K;
  snd.render(regs, out, 16);
  TEST_ASSERT_TRUE(out[0] != 0);
  TEST_ASSERT_EQUAL(out[0], out[8]);
  TEST_ASSERT_EQUAL(-out[0], out[4]);
}

// full thrust plus explosion stays in range and is not silent
static void test_thrust_bounded(void) {
  regs[LL_SND_CTRL] = LL_THRUST_MASK | LL_EXPLODE;
  snd.render(regs, out, 480);
  int peak = 0;
  for (int i = 0; i < 480; i++) {
    TEST_ASSERT_TRUE(out[i] <= 511 && out[i] >= -511);
    peak = out[i] > peak ? out[i] : (-out[i] > peak ? -out[i] : peak);
  }
  TEST_ASSERT_TRUE(peak > 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_silent);
  RUN_TEST(test_tone_3k);
  RUN_TEST(test_thrust_bounded);
  return UNITY_END();
}
