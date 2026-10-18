#include <unity.h>
#include <string.h>

#include "../../src/machines/gng/gng_hud.h"

static const uint8_t SPACE = 0x20;
static const uint8_t LIFE_TOP_L = 0x8a, LIFE_TOP_R = 0x8b;

static uint8_t code[32];
static uint8_t attr[32];
static uint32_t blank[1024 / 32];

void setUp(void) {
  memset(code, SPACE, sizeof(code));
  memset(attr, 0, sizeof(attr));
  memset(blank, 0, sizeof(blank));
  blank[SPACE >> 5] |= 1u << (SPACE & 31);
}
void tearDown(void) {}

// MAME 2026-10-10, fg row 28 in game: two lives at native columns 0-3,
// weapon frame at 14-17
static void test_lives_run(void) {
  for (int c = 0; c < 4; c += 2) {
    code[c] = LIFE_TOP_L;
    code[c + 1] = LIFE_TOP_R;
  }
  memset(code + 14, 0xa0, 4);

  const uint8_t lead = gngLeadRun(code, attr, blank);
  TEST_ASSERT_EQUAL_UINT8(4, lead);

  // lives 8 px right: panel 0-3 = native 0-3, native 4 (blank) dropped
  TEST_ASSERT_EQUAL_UINT8(0, gngFgColumn(0, lead));
  TEST_ASSERT_EQUAL_UINT8(3, gngFgColumn(3, lead));
  TEST_ASSERT_EQUAL_UINT8(5, gngFgColumn(4, lead));
  TEST_ASSERT_EQUAL_UINT8(14, gngFgColumn(13, lead)); // weapon frame stays
}

// native column 0 blank: the usual 8 px crop
static void test_no_run(void) {
  code[2] = 'P';
  const uint8_t lead = gngLeadRun(code, attr, blank);
  TEST_ASSERT_EQUAL_UINT8(0, lead);
  TEST_ASSERT_EQUAL_UINT8(1, gngFgColumn(0, lead));
  TEST_ASSERT_EQUAL_UINT8(GNG_FG_COLS, gngFgColumn(GNG_FG_COLS - 1, lead));
}

// attr bits 6-7 select the char bank: 0x20 in bank 1 is not blank
static void test_bank(void) {
  attr[0] = 0x40;
  TEST_ASSERT_EQUAL_UINT8(1, gngLeadRun(code, attr, blank));
}

// full row (boot RAM test): the run stops at the panel width, every
// panel column maps inside the row
static void test_full_row(void) {
  memset(code, 0x33, sizeof(code));
  const uint8_t lead = gngLeadRun(code, attr, blank);
  TEST_ASSERT_EQUAL_UINT8(GNG_FG_COLS, lead);
  TEST_ASSERT_EQUAL_UINT8(GNG_FG_COLS - 1, gngFgColumn(GNG_FG_COLS - 1, lead));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_lives_run);
  RUN_TEST(test_no_run);
  RUN_TEST(test_bank);
  RUN_TEST(test_full_row);
  return UNITY_END();
}
