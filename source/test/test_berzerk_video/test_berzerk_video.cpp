#include <unity.h>

#include "../../src/machines/berzerk/berzerk_video.h"

using namespace berzerk_video;

void setUp(void) {}
void tearDown(void) {}

// MAME ttl74181_device::update() with M = 1, one bit, F output
static int gate_bit(int a, int b, int s) {
  const int s0 = s & 1, s1 = s >> 1 & 1, s2 = s >> 2 & 1, s3 = s >> 3 & 1;
  const int ap = !(a | (b & s0) | (s1 & !b));
  const int bp = !((!b & s2 & a) | (a & b & s3));
  return 1 ^ (!ap & bp);
}

static void test_alu_matches_gate_model(void) {
  for (int s = 0; s < 16; s++) {
    for (int a = 0; a < 256; a++) {
      for (int b = 0; b < 256; b++) {
        uint8_t f = 0;
        for (int i = 0; i < 8; i++) {
          f |= gate_bit(a >> i & 1, b >> i & 1, s) << i;
        }
        TEST_ASSERT_EQUAL_UINT8(f ^ 0xff, alu(a, b, s));
      }
    }
  }
}

static void test_shift_and_flip(void) {
  TEST_ASSERT_EQUAL_UINT8(0x80, shift(0x00, 0x80, 0));
  TEST_ASSERT_EQUAL_UINT8(0x10, shift(0x00, 0x80, 3));
  TEST_ASSERT_EQUAL_UINT8(0xc0, shift(0x01, 0x80, 1)); // last data fills the MSBs
  TEST_ASSERT_EQUAL_UINT8(0x01, shift(0x00, 0x80, 8)); // bit 3: reversed
}

static void test_column_map(void) {
  int16_t map[256];
  buildColumnMap(map);
  for (int x = 0; x < CROP; x++) {
    TEST_ASSERT_EQUAL_INT16(-1, map[x]);
    TEST_ASSERT_EQUAL_INT16(-1, map[255 - x]);
  }
  TEST_ASSERT_EQUAL_INT16(0, map[CROP]);
  TEST_ASSERT_EQUAL_INT16(OUT_WIDTH - 1, map[255 - CROP]);

  int merges = 0;
  for (int x = CROP + 1; x < 256 - CROP; x++) {
    const int d = map[x] - map[x - 1];
    TEST_ASSERT_TRUE(d == 0 || d == 1);
    if (d == 0) {
      merges++;
      TEST_ASSERT_EQUAL_INT(x >> 2, (x - 1) >> 2); // same color nibble
    }
  }
  TEST_ASSERT_EQUAL_INT(MERGES, merges);
}

// every output column gets a native column: a 1 px laser never vanishes
static void test_merge_keeps_single_pixel(void) {
  int16_t map[256];
  buildColumnMap(map);
  bool hit[OUT_WIDTH] = {};
  for (int x = CROP; x < 256 - CROP; x++) {
    hit[map[x]] = true;
  }
  for (int o = 0; o < OUT_WIDTH; o++) {
    TEST_ASSERT_TRUE(hit[o]);
  }
}

static void test_pens(void) {
  TEST_ASSERT_EQUAL_HEX16(0x0000, pen(0));
  TEST_ASSERT_EQUAL_HEX16(0xe007, pen(0x0a)); // G + I: (0, 255, 0)
  TEST_ASSERT_EQUAL_HEX16(0x0d00, pen(0x04)); // B: (0, 0, 108)
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_alu_matches_gate_model);
  RUN_TEST(test_shift_and_flip);
  RUN_TEST(test_column_map);
  RUN_TEST(test_merge_keeps_single_pixel);
  RUN_TEST(test_pens);
  return UNITY_END();
}
