#include <unity.h>
#include "../../src/hal/rp2350/sample_ring.h"

using hal::hdmi::SampleRing;

static SampleRing ring;

void setUp(void) { ring.clear(); }
void tearDown(void) {}

static void test_empty_pop_fails(void) {
  int16_t out[2] = {7, 7};
  TEST_ASSERT_FALSE(ring.pop(out, 2));
  TEST_ASSERT_EQUAL_INT16(7, out[0]);
}

static void test_fifo_order(void) {
  const int16_t in[4] = {1, -2, 3, -4};
  TEST_ASSERT_TRUE(ring.push(in, 4));
  int16_t out[2];
  TEST_ASSERT_TRUE(ring.pop(out, 2));
  TEST_ASSERT_EQUAL_INT16(1, out[0]);
  TEST_ASSERT_EQUAL_INT16(-2, out[1]);
  TEST_ASSERT_TRUE(ring.pop(out, 2));
  TEST_ASSERT_EQUAL_INT16(3, out[0]);
  TEST_ASSERT_EQUAL_INT16(-4, out[1]);
  TEST_ASSERT_FALSE(ring.pop(out, 2));
}

static void test_push_all_or_nothing(void) {
  int16_t chunk[64] = {0};
  uint32_t pushed = 0;
  while (ring.push(chunk, 64)) {
    pushed += 64;
  }
  TEST_ASSERT_EQUAL_UINT32(SampleRing::SIZE, pushed);
  TEST_ASSERT_EQUAL_UINT32(0, ring.space());

  int16_t out[2];
  TEST_ASSERT_TRUE(ring.pop(out, 2));
  TEST_ASSERT_FALSE(ring.push(chunk, 64));  // 2 free, chunk needs 64
  TEST_ASSERT_EQUAL_UINT32(2, ring.space());
}

static void test_wraps(void) {
  int16_t chunk[64];
  int16_t out[2];
  int16_t next = 0;
  int16_t expect = 0;
  for (int round = 0; round < 100; round++) {
    for (int i = 0; i < 64; i++) {
      chunk[i] = next++;
    }
    TEST_ASSERT_TRUE(ring.push(chunk, 64));
    for (int i = 0; i < 32; i++) {
      TEST_ASSERT_TRUE(ring.pop(out, 2));
      TEST_ASSERT_EQUAL_INT16(expect++, out[0]);
      TEST_ASSERT_EQUAL_INT16(expect++, out[1]);
    }
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_pop_fails);
  RUN_TEST(test_fifo_order);
  RUN_TEST(test_push_all_or_nothing);
  RUN_TEST(test_wraps);
  return UNITY_END();
}
