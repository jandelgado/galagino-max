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
  m.start(s);
  m.next();
  m.start(s);
  m.next();
  m.start(s); // both busy: replaces the first (oldest)
  TEST_ASSERT_EQUAL_INT32(2 * 16, m.next());
}

void test_sreset_stops_all() {
  FxMixer m;
  const FxSample s = { SHORT_FX, 5 };
  m.start(s);
  m.start(s);
  m.stopAll();
  TEST_ASSERT_EQUAL_INT32(0, m.next());
}

void test_mixer_ends_sample() {
  FxMixer m;
  const FxSample s = { SHORT_FX, 1 };
  m.start(s);
  TEST_ASSERT_EQUAL_INT32(16, m.next());
  TEST_ASSERT_EQUAL_INT32(0, m.next());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fx_ring_two_per_frame);
  RUN_TEST(test_fx_ring_full_drops);
  RUN_TEST(test_mixer_replaces_oldest);
  RUN_TEST(test_sreset_stops_all);
  RUN_TEST(test_mixer_ends_sample);
  return UNITY_END();
}
