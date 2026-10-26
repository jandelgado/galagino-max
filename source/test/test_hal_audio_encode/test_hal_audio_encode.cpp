#include <unity.h>
#include "../../src/hal/esp/audio_encode.h"

void setUp(void) {}
void tearDown(void) {}

static const int N = 64;
static const int16_t SAMPLES[] = {-32768, -1, 0, 1, 2133, 32767};

// The old Arduino SND_DIFF words: positive and negative channel interleaved.
static void oldDiff(const int16_t *in, uint16_t *out) {
  const uint16_t SILENCE = 0x8000;
  for (int i = 0; i < N; i++) {
    out[2 * i] = SILENCE + in[i];
    out[2 * i + 1] = SILENCE - in[i];
  }
}

// IDF built-in DAC: the driver widens byte k to the high byte of 16 bit DMA
// word k (CONFIG_DAC_DMA_AUTO_16BIT_ALIGN). Mono plays the words in order (no
// pair swap, heard on the device); SND_DIFF keeps the old channel order.
static void test_dac8_mono_is_high_byte_in_order(void) {
  for (int16_t v : SAMPLES) {
    int16_t in[N];
    uint8_t got[N];
    for (int i = 0; i < N; i++) {
      in[i] = (int16_t)(v + 37 * i);
    }
    hal::esp::encodeDac8Mono(in, got, N);
    for (int k = 0; k < N; k++) {
      TEST_ASSERT_EQUAL_UINT8((uint16_t)(hal::esp::SILENCE + in[k]) >> 8, got[k]);
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
  TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x80, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, out[3]);
}

static void test_silence_is_idle_level(void) {
  int16_t in[N] = {};
  uint8_t out[N];
  hal::esp::encodeDac8Mono(in, out, N);
  for (int i = 0; i < N; i++) {
    TEST_ASSERT_EQUAL_HEX8(hal::esp::SILENCE >> 8, out[i]);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_dac8_mono_is_high_byte_in_order);
  RUN_TEST(test_dac8_diff_is_high_byte_of_old_word);
  RUN_TEST(test_dac8_full_scale);
  RUN_TEST(test_silence_is_idle_level);
  return UNITY_END();
}
