#include <unity.h>

#include "../../src/emulation/speech_ring.h"

static const uint32_t IN_HZ = 8400, OUT_HZ = 24000;
static SpeechRing ring;

void setUp(void) { ring.reset(IN_HZ, OUT_HZ); }
void tearDown(void) {}

static void push_n(int n, int16_t v) {
  for (int i = 0; i < n; i++) {
    ring.push(v);
  }
}

// silent until TARGET samples are queued, nothing consumed meanwhile
static void test_prebuffer(void) {
  push_n(SpeechRing::TARGET - 1, 100);
  int16_t out[64];
  ring.render(out, 64);
  for (int i = 0; i < 64; i++) {
    TEST_ASSERT_EQUAL_INT16(0, out[i]);
  }
  TEST_ASSERT_EQUAL_UINT16(SpeechRing::TARGET - 1, ring.count());
}

// at TARGET fill the step is nominal: 64 output samples take 64 * 0.35
static void test_nominal_rate(void) {
  push_n(SpeechRing::TARGET, 100);
  int16_t out[64];
  ring.render(out, 64);
  TEST_ASSERT_EQUAL_UINT16(SpeechRing::TARGET - 22, ring.count());
  TEST_ASSERT_EQUAL_INT16(100, out[63]);
}

// linear interpolation between source samples
static void test_interpolates(void) {
  ring.reset(IN_HZ, IN_HZ * 4); // step 1/4
  ring.push(0);
  push_n(SpeechRing::TARGET - 1, 400);

  // 4 outputs per source sample, a 0 -> 400 edge is a 4 sample ramp
  int16_t out[16];
  ring.render(out, 16);
  const int16_t expect[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 200, 300, 400, 400, 400, 400};
  TEST_ASSERT_EQUAL_INT16_ARRAY(expect, out, 16);
}

// a producer running faster than nominal: the step rises, the fill
// settles instead of overflowing
static void test_tracks_fast_producer(void) {
  int16_t out[64];
  // 4% fast: 22.4 * 1.04 per 64 output samples, in whole samples
  uint32_t produced_q8 = 0;
  for (int call = 0; call < 6000; call++) {
    produced_q8 += 5964; // 23.3 << 8
    push_n(produced_q8 >> 8, 7);
    produced_q8 &= 0xff;
    ring.render(out, 64);
    TEST_ASSERT_TRUE(ring.count() < SpeechRing::SIZE - 64);
  }
  TEST_ASSERT_TRUE(ring.count() > SpeechRing::TARGET);
  TEST_ASSERT_EQUAL_INT16(7, out[63]);
}

// a slower producer: fill settles below TARGET, never runs dry
static void test_tracks_slow_producer(void) {
  int16_t out[64];
  uint32_t produced_q8 = 0;
  push_n(SpeechRing::TARGET, 7);
  for (int call = 0; call < 6000; call++) {
    produced_q8 += 5505; // 21.5 << 8, 4% slow
    push_n(produced_q8 >> 8, 7);
    produced_q8 &= 0xff;
    ring.render(out, 64);
    TEST_ASSERT_TRUE(ring.count() > 0);
  }
  TEST_ASSERT_TRUE(ring.count() < SpeechRing::TARGET);
}

// full ring drops new samples
static void test_overflow_drops(void) {
  push_n(SpeechRing::SIZE + 10, 1);
  TEST_ASSERT_EQUAL_UINT16(SpeechRing::SIZE - 1, ring.count());
}

// underrun holds the last sample and waits for TARGET again
static void test_underrun_rebuffers(void) {
  push_n(SpeechRing::TARGET, 50);
  int16_t out[64];
  for (int i = 0; i < 30; i++) {
    ring.render(out, 64);
  }
  TEST_ASSERT_EQUAL_UINT16(0, ring.count());
  TEST_ASSERT_EQUAL_INT16(50, out[63]);

  push_n(10, 90);
  ring.render(out, 64);
  TEST_ASSERT_EQUAL_UINT16(10, ring.count());
  TEST_ASSERT_EQUAL_INT16(50, out[0]);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_prebuffer);
  RUN_TEST(test_nominal_rate);
  RUN_TEST(test_interpolates);
  RUN_TEST(test_tracks_fast_producer);
  RUN_TEST(test_tracks_slow_producer);
  RUN_TEST(test_overflow_drops);
  RUN_TEST(test_underrun_rebuffers);
  return UNITY_END();
}
