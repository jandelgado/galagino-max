#include <sys/wait.h>
#include <unistd.h>
#include <unity.h>

// One-pool layout (B = 0, e.g. rp2350): init() reserves no block B, first
// fit and the scratch stop at the end of A, ARENA_B allocations abort.
#undef ARENA_B_SIZE
#define ARENA_B_SIZE 0
#include "../../src/emulation/arena.cpp"

void setUp(void) { Arena::reset(); }
void tearDown(void) {}

static void test_sizes(void) {
  TEST_ASSERT_EQUAL_UINT32(0, Arena::BLOCK_B_SIZE);
  TEST_ASSERT_EQUAL_UINT32(Arena::BLOCK_A_SIZE, Arena::CAPACITY);
}

static void test_first_fit_ends_with_a(void) {
  uint8_t *a = Arena::alloc(Arena::BLOCK_A_SIZE);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NULL(Arena::scratch(16));  // no gap in A, no block B: heap fallback
}

static void test_reserve_without_b(void) {
  const ArenaPlan plan = {1000, 0};
  Arena::reserve(&plan);
  TEST_ASSERT_NOT_NULL(Arena::alloc(100, 4, ARENA_A));
  TEST_ASSERT_NOT_NULL(Arena::alloc(Arena::BLOCK_A_SIZE - 1000));
}

// abort() in a child: the parent only sees the exit status
static bool aborts(void (*fn)(void)) {
  pid_t pid = fork();
  if (pid == 0) {
    fn();
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static void allocB(void) { Arena::alloc(4, 4, ARENA_B); }
static void allocAnyPastA(void) { Arena::alloc(Arena::BLOCK_A_SIZE + 1); }

static void test_block_b_aborts(void) {
  TEST_ASSERT_TRUE(aborts(allocB));
  TEST_ASSERT_TRUE(aborts(allocAnyPastA));
}

int main(int, char **) {
  Arena::init();  // must not abort with B = 0
  UNITY_BEGIN();
  RUN_TEST(test_sizes);
  RUN_TEST(test_first_fit_ends_with_a);
  RUN_TEST(test_reserve_without_b);
  RUN_TEST(test_block_b_aborts);
  return UNITY_END();
}
