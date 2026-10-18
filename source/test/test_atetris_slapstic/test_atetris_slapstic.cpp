#include <unity.h>

#include "../../src/machines/atetris/atetris_slapstic.h"

void setUp(void) {}
void tearDown(void) {}

static void run(Slapstic101 &s, const uint16_t *a, int n) {
  for (int i = 0; i < n; i++) s.access(a[i]);
}

static void test_direct_needs_reset(void) {
  Slapstic101 s;
  s.reset();
  TEST_ASSERT_EQUAL_UINT8(3, s.bank());
  s.access(0x6090);
  TEST_ASSERT_EQUAL_UINT8(3, s.bank()); // idle: bank address ignored
  const uint16_t seq[] = {0x6000, 0x1234, 0xd5c7, 0x6090};
  run(s, seq, 4);                        // other accesses in between are fine
  TEST_ASSERT_EQUAL_UINT8(1, s.bank());
  s.access(0x6080);
  TEST_ASSERT_EQUAL_UINT8(1, s.bank()); // back to idle after a switch
}

static void test_bitwise(void) {
  Slapstic101 s;
  s.reset();                     // bank 3 = 0b11
  const uint16_t seq[] = {
      0x6000,                    // reset
      0x7540,                    // bitwise start (0x1540, mask 0x1ff0)
      0x6080,                    // load current bank (3)
      0x7540,                    // odd: clear bit 0 -> 2
      0x7541,                    // even: clear bit 1 -> 0
      0x9000,                    // other access: ignored
      0x7541,                    // odd: set bit 0 -> 1
  };
  run(s, seq, 7);
  TEST_ASSERT_EQUAL_UINT8(3, s.bank()); // not committed yet
  s.access(0x7550);                     // commit
  TEST_ASSERT_EQUAL_UINT8(1, s.bank());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_direct_needs_reset);
  RUN_TEST(test_bitwise);
  return UNITY_END();
}
