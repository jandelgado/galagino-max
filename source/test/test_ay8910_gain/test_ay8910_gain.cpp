// Ay8910::setChannelGain scales one voice (MCR SSIO duty-cycle volume).
#include <stdlib.h>
#include <unity.h>

#include "../../src/emulation/ay8910.cpp"

void setUp(void) {}
void tearDown(void) {}

// voice 0 square tone, full volume, others silent
static const uint8_t TONE[16] = {0x40, 0, 0, 0, 0, 0, 0, 0x3e, 0x0f};

static int peak(Ay8910 &ay) {
  int16_t out[AUDIO_CHUNK];
  ay.render(TONE, out, AUDIO_CHUNK);
  int p = 0;
  for (int i = 0; i < AUDIO_CHUNK; i++) {
    if (abs(out[i]) > p) {
      p = abs(out[i]);
    }
  }
  return p;
}

static void test_default_gain_unchanged(void) {
  Ay8910 ay(10, 2);
  TEST_ASSERT_EQUAL(2 * 15, peak(ay));
}

static void test_half_gain_halves_voice(void) {
  Ay8910 ay(10, 2);
  ay.setChannelGain(0, 8);
  TEST_ASSERT_EQUAL(15, peak(ay));
}

static void test_other_voice_gain_no_effect(void) {
  Ay8910 ay(10, 2);
  ay.setChannelGain(1, 0);
  TEST_ASSERT_EQUAL(2 * 15, peak(ay));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_default_gain_unchanged);
  RUN_TEST(test_half_gain_halves_voice);
  RUN_TEST(test_other_voice_gain_no_effect);
  return UNITY_END();
}
