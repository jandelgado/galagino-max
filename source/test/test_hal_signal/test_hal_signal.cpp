#include <unity.h>
#include <chrono>
#include <thread>
#include "../../src/hal/hal.h"

void setUp(void) {}
void tearDown(void) {}

// Latched event, as the former ulTaskNotifyTake(1, ...): several gives before a
// take collapse into one.
static void test_give_is_latched_not_counted(void) {
  hal::Signal s;
  s.give();
  s.give();
  TEST_ASSERT_TRUE(s.take(0));
  TEST_ASSERT_FALSE(s.take(0));
}

static void test_take_times_out(void) {
  hal::Signal s;
  const auto t0 = std::chrono::steady_clock::now();
  TEST_ASSERT_FALSE(s.take(20));
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  TEST_ASSERT_TRUE(ms >= 15);
}

static void test_clear_drops_pending(void) {
  hal::Signal s;
  s.give();
  s.clear();
  TEST_ASSERT_FALSE(s.take(0));
}

static void test_give_from_other_thread_wakes_take(void) {
  hal::Signal s;
  std::thread t([&s] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    s.give();
  });
  TEST_ASSERT_TRUE(s.take(2000));
  t.join();
}

static void test_wait_forever_constant(void) {
  hal::Signal s;
  s.give();
  TEST_ASSERT_TRUE(s.take(hal::WAIT_FOREVER));
}

static void test_critical_section_scope(void) {
  int n = 0;
  {
    hal::CriticalSection guard;
    n++;
  }
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT(hal::EMULATION_CORE, 1 - hal::VIDEO_CORE);
  hal::sleepMs(1);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_give_is_latched_not_counted);
  RUN_TEST(test_take_times_out);
  RUN_TEST(test_clear_drops_pending);
  RUN_TEST(test_give_from_other_thread_wakes_take);
  RUN_TEST(test_wait_forever_constant);
  RUN_TEST(test_critical_section_scope);
  return UNITY_END();
}
