#include <unity.h>

#include "../../src/emulation/spinner.h"

void setUp(void) {}
void tearDown(void) {}

// Tempest: 4 bit counter, 0.5 .. 6 counts per frame
static Spinner tempest() { return Spinner(2, 24, 1); }
// Arkanoid (arkanoid::SPIN_*): 8 bit paddle counter, 1 .. 2.5 counts per frame
static Spinner arkanoid() { return Spinner(4, 10, 1); }

// signed 4 bit difference, as Tempest's IRQ handler computes it
static int delta4(uint8_t from, uint8_t to) {
  const int d = (to - from) & 0x0f;
  return d < 8 ? d : d - 16;
}

static int delta8(uint8_t from, uint8_t to) { return (int8_t)(uint8_t)(to - from); }

// holding a direction accelerates but never moves more than 7 counts per
// frame (the game reads a signed 4 bit difference)
static void test_accelerates_bounded(void) {
  Spinner s = tempest();
  int first = 0, last = 0;
  for (int f = 0; f < 200; f++) {
    const uint8_t before = s.count();
    s.step(1);
    const int d = delta4(before, s.count());
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
  Spinner r = tempest(), l = tempest();
  int right = 0, left = 0;
  for (int f = 0; f < 30; f++) {
    const uint8_t rb = r.count(), lb = l.count();
    r.step(1);
    l.step(-1);
    right += delta4(rb, r.count());
    left += delta4(lb, l.count());
  }
  TEST_ASSERT_GREATER_THAN(0, right);
  TEST_ASSERT_INT_WITHIN(1, -right, left);
}

// counts moved in 4 frames of `then`, after 50 frames of `hold` and one
// frame of `gap`
static int moved_after(int hold, int gap, int then) {
  Spinner s = tempest();
  for (int f = 0; f < 50; f++) {
    s.step(hold);
  }
  s.step(gap);
  int moved = 0;
  for (int f = 0; f < 4; f++) {
    const uint8_t before = s.count();
    s.step(then);
    moved += delta4(before, s.count());
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

// Tempest counts as with the former tempest_spinner.h: 30 right, 5
// released, 40 left, 1 right
static void test_tempest_params_unchanged(void) {
  static const uint8_t GOLDEN[] = {
      0, 1, 2, 3, 5, 6, 8, 11, 13, 0, 3, 6, 10, 13, 1, 6, 10, 15, 4,
      9, 15, 4, 10, 0, 6, 12, 2, 8, 14, 4, 4, 4, 4, 4, 4, 4, 3, 2,
      1, 15, 14, 12, 9, 7, 4, 1, 14, 10, 7, 3, 14, 10, 5, 0, 11, 5, 0,
      10, 4, 14, 8, 2, 12, 6, 0, 10, 4, 14, 8, 2, 12, 6, 0, 10, 4, 4};
  Spinner s = tempest();
  int i = 0;
  for (int f = 0; f < 76; f++) {
    const int8_t dir = f < 30 ? 1 : f < 35 ? 0 : f < 75 ? -1 : 1;
    s.step(dir);
    TEST_ASSERT_EQUAL_UINT8(GOLDEN[i++], s.count() & 0x0f);
  }
}

// Arkanoid reads the 8 bit counter: every held frame moves forward, never
// half the counter range or more (would read as a reversal)
static void test_arkanoid_step_bounded(void) {
  Spinner s = arkanoid();
  for (int f = 0; f < 300; f++) {
    const uint8_t before = s.count();
    s.step(1);
    const int d = delta8(before, s.count());
    TEST_ASSERT_TRUE(d >= 1 && d <= 63);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_accelerates_bounded);
  RUN_TEST(test_direction);
  RUN_TEST(test_release_resets);
  RUN_TEST(test_tempest_params_unchanged);
  RUN_TEST(test_arkanoid_step_bounded);
  return UNITY_END();
}
