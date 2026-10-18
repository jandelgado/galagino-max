#include <unity.h>
#include "../../src/machines/bublbobl/bublbobl_sound.h"

using namespace bublbobl_sound;

// predictor 0x1000 (>> 8 = 16), then nibbles 0: step 0 keeps it at 16
static const uint8_t SHORT_FX[] = { 0x00, 0x10, 0x00, 0x00 };

void setUp() {}
void tearDown() {}

void test_fx_ring_two_per_frame() {
  FxRing r;
  r.push(3);
  r.push(5);
  uint8_t fx;
  TEST_ASSERT_TRUE(r.pop(fx));
  TEST_ASSERT_EQUAL_UINT8(3, fx);
  TEST_ASSERT_TRUE(r.pop(fx));
  TEST_ASSERT_EQUAL_UINT8(5, fx);
  TEST_ASSERT_FALSE(r.pop(fx));
}

void test_fx_ring_full_drops() {
  FxRing r;
  for (uint8_t i = 0; i < 10; i++) {
    r.push(i);
  }
  uint8_t fx, n = 0;
  while (r.pop(fx)) {
    n++;
  }
  TEST_ASSERT_EQUAL_UINT8(7, n); // 8 slots, one kept empty
}

void test_mixer_replaces_oldest() {
  FxMixer m;
  const FxSample s = { SHORT_FX, 5 };
  m.start(s, 0);
  m.next();
  m.start(s, 1);
  m.next();
  m.start(s, 2); // both busy: replaces the first (oldest)
  TEST_ASSERT_EQUAL_INT32(2 * 16, m.next());
}

// rapid fire of one effect restarts its own channel and keeps the
// other effect playing
void test_same_effect_restarts_own_channel() {
  FxMixer m;
  const FxSample shot = { SHORT_FX, 5 };
  const FxSample other = { SHORT_FX, 5 };
  m.start(other, 1);
  m.start(shot, 2);
  m.start(shot, 2);
  m.start(shot, 2);
  TEST_ASSERT_EQUAL_UINT8(1, m.ch[0].fx);
  TEST_ASSERT_EQUAL_UINT8(2, m.ch[1].fx);
  TEST_ASSERT_EQUAL_INT32(2 * 16, m.next());
}

void test_sreset_stops_all() {
  FxMixer m;
  const FxSample s = { SHORT_FX, 5 };
  m.start(s, 0);
  m.start(s, 1);
  m.stopAll();
  TEST_ASSERT_EQUAL_INT32(0, m.next());
}

// one stored (12 kHz) sample lasts two 24 kHz output samples
void test_mixer_ends_sample() {
  FxMixer m;
  const FxSample s = { SHORT_FX, 1 };
  m.start(s, 0);
  TEST_ASSERT_EQUAL_INT32(16, m.next());
  TEST_ASSERT_EQUAL_INT32(16, m.next());
  TEST_ASSERT_EQUAL_INT32(0, m.next());
}

// predictor 0, then nibbles 7: the predictor climbs ever faster
static const uint8_t RISING_FX[] = { 0x00, 0x00, 0x77, 0x77, 0x77, 0x77, 0x77 };
static const uint8_t RISING_LEN = 11;

// output: s0, s0, (s0 + s1) / 2, s1, (s1 + s2) / 2, s2, ...
void test_mixer_interpolates_midpoint() {
  int16_t s[RISING_LEN] = { 0 };
  int16_t p = s[0];
  int8_t step = 0;
  for (uint8_t i = 1; i < RISING_LEN; i++) {
    ima_adpcm::ima_adpcm_decode(p, step, 7);
    s[i] = p;
  }

  FxMixer m;
  const FxSample fx = { RISING_FX, RISING_LEN };
  m.start(fx, 0);
  TEST_ASSERT_EQUAL_INT32(s[0] >> 8, m.next());
  TEST_ASSERT_EQUAL_INT32(s[0] >> 8, m.next());
  for (uint8_t i = 1; i < RISING_LEN; i++) {
    TEST_ASSERT_EQUAL_INT32((s[i - 1] + s[i]) >> 9, m.next());
    TEST_ASSERT_EQUAL_INT32(s[i] >> 8, m.next());
  }
  TEST_ASSERT_EQUAL_INT32(0, m.next());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fx_ring_two_per_frame);
  RUN_TEST(test_fx_ring_full_drops);
  RUN_TEST(test_mixer_replaces_oldest);
  RUN_TEST(test_same_effect_restarts_own_channel);
  RUN_TEST(test_sreset_stops_all);
  RUN_TEST(test_mixer_ends_sample);
  RUN_TEST(test_mixer_interpolates_midpoint);
  return UNITY_END();
}
