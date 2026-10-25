#include <unity.h>
#include "../../src/hal/esp/audio_encode.h"

void setUp(void) {}
void tearDown(void) {}

static const int N = 64;
static const int16_t SAMPLES[] = {-32768, -1, 0, 1, 2133, 32767};

// The former Audio::valueToBuffer, mono: value after volume, bias SILENCE,
// byte-order workaround (index ^ 1).
static void oldMono(const int16_t *in, uint16_t *out) {
  const uint16_t SILENCE = 0x8000;
  for (int i = 0; i < N; i++) {
    out[i ^ 1] = SILENCE + in[i];
  }
}

// The former SND_DIFF layout: positive and negative channel interleaved.
static void oldDiff(const int16_t *in, uint16_t *out) {
  const uint16_t SILENCE = 0x8000;
  for (int i = 0; i < N; i++) {
    out[2 * i] = SILENCE + in[i];
    out[2 * i + 1] = SILENCE - in[i];
  }
}

static void test_mono_matches_old_formula(void) {
  for (int16_t v : SAMPLES) {
    int16_t in[N];
    uint16_t want[N], got[N];
    for (int i = 0; i < N; i++) {
      in[i] = (int16_t)(v + i);
    }
    oldMono(in, want);
    hal::esp::encodeMono(in, got, N);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(want, got, N);
  }
}

static void test_diff_matches_old_formula(void) {
  for (int16_t v : SAMPLES) {
    int16_t in[N];
    uint16_t want[2 * N], got[2 * N];
    for (int i = 0; i < N; i++) {
      in[i] = (int16_t)(v + i);
    }
    oldDiff(in, want);
    hal::esp::encodeDiff(in, got, N);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(want, got, 2 * N);
  }
}

static void test_silence_is_idle_level(void) {
  int16_t in[N] = {};
  uint16_t out[N];
  hal::esp::encodeMono(in, out, N);
  for (int i = 0; i < N; i++) {
    TEST_ASSERT_EQUAL_HEX16(hal::esp::SILENCE, out[i]);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_mono_matches_old_formula);
  RUN_TEST(test_diff_matches_old_formula);
  RUN_TEST(test_silence_is_idle_level);
  return UNITY_END();
}
