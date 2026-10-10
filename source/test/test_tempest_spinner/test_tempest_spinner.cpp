#include <unity.h>

#include "../../src/machines/tempest/tempest_spinner.h"

void setUp(void) {}
void tearDown(void) {}

// signed 4 bit difference, as the game's IRQ handler computes it
static int delta(uint8_t from, uint8_t to) {
  const int d = (to - from) & 0x0f;
  return d < 8 ? d : d - 16;
}

// holding a direction accelerates but never moves more than 7 counts per
// frame (the game reads a signed 4 bit difference)
static void test_accelerates_bounded(void) {
  Spinner s;
  int first = 0, last = 0;
  for (int f = 0; f < 200; f++) {
    const uint8_t before = s.count();
    s.step(1);
    const int d = delta(before, s.count());
    TEST_ASSERT_TRUE(d >= 0 && d <= 7);
    if (f < 8) {
      first += d;
    }
    if (f >= 192) {
      last += d;
    }
  }
  TEST_ASSERT_LESS_THAN(last, first);
}

// left turns the other way, by the same amount (within the quarter count
// rounding)
static void test_direction(void) {
  Spinner r, l;
  int right = 0, left = 0;
  for (int f = 0; f < 30; f++) {
    const uint8_t rb = r.count(), lb = l.count();
    r.step(1);
    l.step(-1);
    right += delta(rb, r.count());
    left += delta(lb, l.count());
  }
  TEST_ASSERT_GREATER_THAN(0, right);
  TEST_ASSERT_INT_WITHIN(1, -right, left);
}

// counts moved in 4 frames of `then`, after 50 frames of `hold` and one
// frame of `gap`
static int moved_after(int hold, int gap, int then) {
  Spinner s;
  for (int f = 0; f < 50; f++) {
    s.step(hold);
  }
  s.step(gap);
  int moved = 0;
  for (int f = 0; f < 4; f++) {
    const uint8_t before = s.count();
    s.step(then);
    moved += delta(before, s.count());
  }
  return moved;
}

static void test_release_resets(void) {
  const int fresh = moved_after(0, 0, 1);
  TEST_ASSERT_NOT_EQUAL(0, fresh);
  TEST_ASSERT_LESS_THAN(moved_after(1, 1, 1), fresh);
  TEST_ASSERT_INT_WITHIN(1, fresh, moved_after(1, 0, 1)); // fraction carries over
  TEST_ASSERT_INT_WITHIN(1, -fresh, moved_after(1, 1, -1));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_accelerates_bounded);
  RUN_TEST(test_direction);
  RUN_TEST(test_release_resets);
  return UNITY_END();
}
