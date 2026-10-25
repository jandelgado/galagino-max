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

// IDF built-in DAC: the driver widens byte k to the high byte of 16 bit DMA
// word k (CONFIG_DAC_DMA_AUTO_16BIT_ALIGN). The DAC DMA programs the I2S FIFO
// like the old driver (mono: tx_fifo_mod 1, SND_DIFF: 0), so byte k must be the
// high byte of the old word k: same pair swap, same channel order.
static void test_dac8_mono_is_high_byte_of_old_word(void) {
  for (int16_t v : SAMPLES) {
    int16_t in[N];
    uint16_t word[N];
    uint8_t got[N];
    for (int i = 0; i < N; i++) {
      in[i] = (int16_t)(v + 37 * i);
    }
    oldMono(in, word);
    hal::esp::encodeDac8Mono(in, got, N);
    for (int k = 0; k < N; k++) {
      TEST_ASSERT_EQUAL_UINT8(word[k] >> 8, got[k]);
    }
  }
}

static void test_dac8_diff_is_high_byte_of_old_word(void) {
  for (int16_t v : SAMPLES) {
    int16_t in[N];
    uint16_t word[2 * N];
    uint8_t got[2 * N];
    for (int i = 0; i < N; i++) {
      in[i] = (int16_t)(v + 37 * i);
    }
    oldDiff(in, word);
    hal::esp::encodeDac8Diff(in, got, N);
    for (int k = 0; k < 2 * N; k++) {
      TEST_ASSERT_EQUAL_UINT8(word[k] >> 8, got[k]);
    }
  }
}

// full scale reaches both DAC rails without wrapping
static void test_dac8_full_scale(void) {
  const int16_t in[4] = {-32768, 32767, 0, -1};
  uint8_t out[4];
  hal::esp::encodeDac8Mono(in, out, 4);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);  // in[1], pair swapped
  TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x80, out[3]);
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
  RUN_TEST(test_dac8_mono_is_high_byte_of_old_word);
  RUN_TEST(test_dac8_diff_is_high_byte_of_old_word);
  RUN_TEST(test_dac8_full_scale);
  RUN_TEST(test_silence_is_idle_level);
  return UNITY_END();
}
