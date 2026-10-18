#include <unity.h>
#include "../../src/emulation/ima_adpcm.h"

using ima_adpcm::Voice;

// predictor 0x1000 (>> 8 = 16), then nibbles 7, 7, 9, 0 (low nibble first)
static const uint8_t STREAM[] = { 0x00, 0x10, 0x77, 0x09 };
static const uint32_t COUNT = 5;

void setUp() {}
void tearDown() {}

// reference: decode the stream sample by sample
static void reference(int16_t out[COUNT]) {
  int16_t predictor = (int16_t)(STREAM[0] | (STREAM[1] << 8));
  int8_t step = 0;
  out[0] = predictor >> 8;
  for (uint32_t n = 0; n < COUNT - 1; n++) {
    const uint8_t packed = STREAM[2 + (n >> 1)];
    ima_adpcm::ima_adpcm_decode(predictor, step, (n & 1) ? (packed >> 4) : (packed & 0x0f));
    out[n + 1] = predictor >> 8;
  }
}

void test_idle_is_silent() {
  Voice v;
  TEST_ASSERT_FALSE(v.active());
  TEST_ASSERT_EQUAL_INT16(0, v.next());
}

void test_plays_stream_once() {
  int16_t ref[COUNT];
  reference(ref);

  Voice v;
  v.start(STREAM, COUNT);
  TEST_ASSERT_TRUE(v.active());
  for (uint32_t i = 0; i < COUNT; i++) {
    TEST_ASSERT_EQUAL_INT16(ref[i], v.next());
  }
  TEST_ASSERT_FALSE(v.active());
  TEST_ASSERT_EQUAL_INT16(0, v.next());
}

void test_hold_repeats_each_sample() {
  int16_t ref[COUNT];
  reference(ref);

  Voice v;
  v.start(STREAM, COUNT, 2);
  for (uint32_t i = 0; i < COUNT; i++) {
    TEST_ASSERT_EQUAL_INT16(ref[i], v.next());
    TEST_ASSERT_TRUE(v.active());
    TEST_ASSERT_EQUAL_INT16(ref[i], v.next());
  }
  TEST_ASSERT_FALSE(v.active());
}

void test_restart_mid_play() {
  int16_t ref[COUNT];
  reference(ref);

  Voice v;
  v.start(STREAM, COUNT);
  v.next();
  v.next();
  v.next();
  v.start(STREAM, COUNT);
  for (uint32_t i = 0; i < COUNT; i++) {
    TEST_ASSERT_EQUAL_INT16(ref[i], v.next());
  }
}

void test_stop_silences() {
  Voice v;
  v.start(STREAM, COUNT);
  v.next();
  v.stop();
  TEST_ASSERT_EQUAL_INT16(0, v.next());
  TEST_ASSERT_FALSE(v.active());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_is_silent);
  RUN_TEST(test_plays_stream_once);
  RUN_TEST(test_hold_repeats_each_sample);
  RUN_TEST(test_restart_mid_play);
  RUN_TEST(test_stop_silences);
  return UNITY_END();
}
