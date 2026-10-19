#include <unity.h>

// Arena reservation and planned blocks (src/emulation/arena.cpp). Block
// bases are found without extra API: after reset() the first first-fit
// allocation starts block A, one larger than A starts block B.
#include "../../src/emulation/arena.cpp"

static uint8_t *baseA;
static uint8_t *baseB;

void setUp(void) {
  Arena::reset();
  baseA = Arena::alloc(1);
  baseB = Arena::alloc(Arena::BLOCK_A_SIZE);
  Arena::reset();
}

void tearDown(void) {}

static bool inRange(const uint8_t *p, const uint8_t *lo, const uint8_t *hi) {
  return p >= lo && p < hi;
}

static void test_any_first_fit_without_plan(void) {
  TEST_ASSERT_EQUAL_PTR(baseA, Arena::alloc(100));
  TEST_ASSERT_EQUAL_PTR(baseB, Arena::alloc(Arena::BLOCK_A_SIZE));
}

static void test_reserved_region(void) {
  const ArenaPlan p = {1000, 2000};
  Arena::reserve(&p);
  const uint8_t *endA = baseA + Arena::BLOCK_A_SIZE;
  const uint8_t *endB = baseB + Arena::BLOCK_B_SIZE;
  const uint8_t *a = Arena::alloc(100, 4, ARENA_A);
  TEST_ASSERT_TRUE(inRange(a, endA - 1000, endA));
  const uint8_t *b = Arena::alloc(200, 4, ARENA_B);
  TEST_ASSERT_TRUE(inRange(b, endB - 2000, endB));

  // first fit stops below the reservation, even with reserved bytes unused
  TEST_ASSERT_EQUAL_PTR(baseA, Arena::alloc(Arena::BLOCK_A_SIZE - 1000 - 50));
  TEST_ASSERT_EQUAL_PTR(baseB, Arena::alloc(100));
}

static void test_reset_clears_reservation(void) {
  const ArenaPlan p = {1000, 2000};
  Arena::reserve(&p);
  Arena::reset();
  TEST_ASSERT_EQUAL_PTR(baseA, Arena::alloc(Arena::BLOCK_A_SIZE));
}

static void test_null_plan_is_no_reservation(void) {
  Arena::reserve(nullptr);
  TEST_ASSERT_EQUAL_PTR(baseA, Arena::alloc(Arena::BLOCK_A_SIZE));
}

// same key as romconv arena_plan.extras_key (test_arena_plan.py)
static void test_extras_key(void) {
  static const uint32_t extras[] = {49152, 35040};
  TEST_ASSERT_EQUAL_UINT32(49152u * 31 + 35040, arenaExtrasKey(extras, 2, false));
  TEST_ASSERT_EQUAL_UINT32((49152u * 31 + 35040) ^ 0x80000000u, arenaExtrasKey(extras, 2, true));
  TEST_ASSERT_EQUAL_UINT32(0, arenaExtrasKey(extras, 0, false));
}

int main(int, char **) {
  Arena::init();
  UNITY_BEGIN();
  RUN_TEST(test_any_first_fit_without_plan);
  RUN_TEST(test_reserved_region);
  RUN_TEST(test_reset_clears_reservation);
  RUN_TEST(test_null_plan_is_no_reservation);
  RUN_TEST(test_extras_key);
  return UNITY_END();
}
