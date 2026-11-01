#include <unity.h>

// FramePacer: 60.0 Hz deadlines of full-rate screens (16/17/17 ms), restart
// when far behind or after a long pause (half-rate machine).
#include "../../src/hal/frame_pacer.h"

void setUp(void) {}
void tearDown(void) {}

// The caller sleeps whole milliseconds (rounded up): 600 frames still take
// 10 s, within one frame.
static void test_deadlines_average_60_hz(void) {
  hal::FramePacer pacer;
  uint32_t now = 0;
  for (int i = 0; i < 600; i++) {
    const int32_t wait = pacer.next(now);
    if (wait > 0) {
      now += (uint32_t)(wait + 999) / 1000 * 1000;
    }
  }
  TEST_ASSERT_UINT32_WITHIN(hal::FramePacer::FRAME_US, 600u * hal::FramePacer::FRAME_US, now);
}

static void test_far_behind_restarts_without_catch_up(void) {
  hal::FramePacer pacer;
  pacer.next(0);
  TEST_ASSERT_TRUE(pacer.next(1000000) <= 0);                 // one second late: run now
  TEST_ASSERT_EQUAL_INT32(hal::FramePacer::FRAME_US, pacer.next(1000000));
}

// Half rate paces its own screens, next() is not called meanwhile. 40 minutes
// later (beyond 2^31 us) a full-rate frame must not wait for a stale deadline.
static void test_full_rate_after_long_half_rate(void) {
  hal::FramePacer pacer;
  pacer.next(0);
  const uint32_t later = 40u * 60u * 1000000u;
  const int32_t wait = pacer.next(later);
  TEST_ASSERT_TRUE(wait <= hal::FramePacer::FRAME_US);
}

static void test_wraps_at_2_32_us(void) {
  hal::FramePacer pacer;
  pacer.next(0xFFFFFF00u);                                    // restart at the first frame
  TEST_ASSERT_EQUAL_INT32(hal::FramePacer::FRAME_US - 0x100, pacer.next(0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_deadlines_average_60_hz);
  RUN_TEST(test_far_behind_restarts_without_catch_up);
  RUN_TEST(test_full_rate_after_long_half_rate);
  RUN_TEST(test_wraps_at_2_32_us);
  return UNITY_END();
}
