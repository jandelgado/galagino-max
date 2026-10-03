#include <thread>
#include <unity.h>

#include "../../src/emulation/seqlock.h"

struct State {
  uint32_t v[64];
};

static const uint32_t STRESS_ITERATIONS = 200000;

void setUp(void) {}
void tearDown(void) {}

static void test_round_trip(void) {
  Seqlock<State> lock;
  lock.publish([](State &s) {
    for (uint32_t i = 0; i < 64; i++) {
      s.v[i] = i;
    }
  });

  State out;
  lock.read(out);
  for (uint32_t i = 0; i < 64; i++) {
    TEST_ASSERT_EQUAL_UINT32(i, out.v[i]);
  }
}

// Writer fills every field with the same counter value; a torn read shows
// up as a snapshot holding two different values.
static void test_no_torn_read(void) {
  Seqlock<State> lock;
  std::atomic<bool> done{false};

  std::thread writer([&] {
    for (uint32_t n = 1; n <= STRESS_ITERATIONS; n++) {
      lock.publish([n](State &s) {
        for (uint32_t i = 0; i < 64; i++) {
          s.v[i] = n;
        }
      });
    }
    done.store(true);
  });

  uint32_t torn = 0;
  uint32_t last = 0;
  bool backwards = false;
  while (!done.load()) {
    State out;
    lock.read(out);
    for (uint32_t i = 1; i < 64; i++) {
      if (out.v[i] != out.v[0]) {
        torn++;
        break;
      }
    }
    if (out.v[0] < last) {
      backwards = true;
    }
    last = out.v[0];
  }
  writer.join();

  TEST_ASSERT_EQUAL_UINT32(0, torn);
  TEST_ASSERT_FALSE(backwards);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip);
  RUN_TEST(test_no_torn_read);
  return UNITY_END();
}
