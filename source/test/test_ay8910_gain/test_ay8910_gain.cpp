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

// Noise only on a voice whose tone period is 0: the tone is off, the
// period does not matter (Super Cobra helicopter).
static void test_noise_only_voice_with_zero_period(void) {
  static const uint8_t NOISE[16] = {0, 0, 0, 0, 0, 0, 8, 0x2f, 0, 0x0f};  // voice B, tone off
  Ay8910 ay(10, 2);
  int16_t out[AUDIO_CHUNK];
  ay.render(NOISE, out, AUDIO_CHUNK);
  int p = 0;
  for (int i = 0; i < AUDIO_CHUNK; i++) {
    p = abs(out[i]) > p ? abs(out[i]) : p;
  }
  TEST_ASSERT_EQUAL(2 * 15, p);
}

// 720 Hz low-pass on a 6 kHz square: the voice is mostly gone, even after the makeup gain
static void test_lowpass_cuts_high_tone(void) {
  static const uint8_t HIGH[16] = {20, 0, 0, 0, 0, 0, 0, 0x3e, 0x0f};  // toggles every 2 samples
  Ay8910 plain(10, 2), filtered(10, 2);
  filtered.setLowpass(0, 720);
  int16_t out[AUDIO_CHUNK];
  plain.render(HIGH, out, AUDIO_CHUNK);
  filtered.render(HIGH, out, AUDIO_CHUNK);  // settle
  const int plain_peak = peak(plain);
  int p = 0;
  filtered.render(HIGH, out, AUDIO_CHUNK);
  for (int i = 0; i < AUDIO_CHUNK; i++) {
    p = abs(out[i]) > p ? abs(out[i]) : p;
  }
  TEST_ASSERT_LESS_THAN(plain_peak / 2, p);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_default_gain_unchanged);
  RUN_TEST(test_half_gain_halves_voice);
  RUN_TEST(test_other_voice_gain_no_effect);
  RUN_TEST(test_noise_only_voice_with_zero_period);
  RUN_TEST(test_lowpass_cuts_high_tone);
  return UNITY_END();
}
