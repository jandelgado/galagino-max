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

// same key as romconv/romconv/plan/arena.py extras_key (romconv/tests/test_plan.py)
static void test_extras_key(void) {
  static const uint32_t extras[] = {49152, 35040};
  TEST_ASSERT_EQUAL_UINT32(49152u * 31 + 35040, arenaExtrasKey(extras, 2, false));
  TEST_ASSERT_EQUAL_UINT32((49152u * 31 + 35040) ^ 0x80000000u, arenaExtrasKey(extras, 2, true));
  TEST_ASSERT_EQUAL_UINT32(0, arenaExtrasKey(extras, 0, false));
}

// LZMA probability table of an unpack: borrowed from the top of the free
// gap, given back after the decode, so it never costs a later allocation
static void test_scratch_is_transient(void) {
  uint8_t *s = Arena::scratch(5228);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_TRUE(inRange(s, baseA, baseA + Arena::BLOCK_A_SIZE));
  TEST_ASSERT_EQUAL_PTR(baseA + Arena::BLOCK_A_SIZE - 5228, s);
  TEST_ASSERT_TRUE(Arena::release(s));
  // whole block A first fit again
  TEST_ASSERT_EQUAL_PTR(baseA, Arena::alloc(Arena::BLOCK_A_SIZE));
}

static void test_scratch_one_at_a_time(void) {
  uint8_t *s = Arena::scratch(5228);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_NULL(Arena::scratch(100));  // the other core: falls back to malloc
  TEST_ASSERT_FALSE(Arena::release(baseB));  // not the scratch: caller frees it
  TEST_ASSERT_TRUE(Arena::release(s));
  TEST_ASSERT_NOT_NULL(Arena::scratch(100));
}

static void test_scratch_below_reservation(void) {
  const ArenaPlan p = {1000, 0};
  Arena::reserve(&p);
  uint8_t *s = Arena::scratch(64);
  TEST_ASSERT_EQUAL_PTR(baseA + Arena::BLOCK_A_SIZE - 1000 - 64, s);
  // allocations meanwhile stay below it
  TEST_ASSERT_EQUAL_PTR(baseA, Arena::alloc(Arena::BLOCK_A_SIZE - 1000 - 64));
  TEST_ASSERT_TRUE(Arena::release(s));
}

static void test_scratch_no_room(void) {
  Arena::alloc(Arena::BLOCK_A_SIZE);
  Arena::alloc(Arena::BLOCK_B_SIZE - 100);
  TEST_ASSERT_NULL(Arena::scratch(5228));  // no abort: heap fallback
  TEST_ASSERT_NOT_NULL(Arena::scratch(100));
}

int main(int, char **) {
  Arena::init();
  UNITY_BEGIN();
  RUN_TEST(test_any_first_fit_without_plan);
  RUN_TEST(test_reserved_region);
  RUN_TEST(test_reset_clears_reservation);
  RUN_TEST(test_null_plan_is_no_reservation);
  RUN_TEST(test_extras_key);
  RUN_TEST(test_scratch_is_transient);
  RUN_TEST(test_scratch_one_at_a_time);
  RUN_TEST(test_scratch_below_reservation);
  RUN_TEST(test_scratch_no_room);
  return UNITY_END();
}
