#include <unity.h>

// input.h needs Arduino.h; same values as there
#define BUTTON_LEFT 0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP 0x04
#define BUTTON_DOWN 0x08
#define BUTTON_FIRE 0x10
#define BUTTON_START 0x20
#define BUTTON_A 0x100
#define BUTTON_B 0x200
#include "../../src/machines/spacduel/spacduel_inputs.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t B7 = 0x80, B6 = 0x40;

// MAME spacduel_IN3_r: P1 shield/fire, left/right, thrust/start, active high
static void test_player1(void) {
  TEST_ASSERT_EQUAL_HEX8(B7, spacduel_in1(0, BUTTON_DOWN, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B7, spacduel_in1(0, BUTTON_B, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B6, spacduel_in1(0, BUTTON_FIRE, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B7, spacduel_in1(2, BUTTON_LEFT, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B6, spacduel_in1(2, BUTTON_RIGHT, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B7, spacduel_in1(4, BUTTON_UP, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B6, spacduel_in1(4, BUTTON_START, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(0, spacduel_in1(1, 0x3ff, SD_SELECTED)); // player 2
  TEST_ASSERT_EQUAL_HEX8(0, spacduel_in1(3, 0x3ff, SD_SELECTED));
}

// DSW2 0x0b: only "1 player game only" (offset 7 bit 6) is on
static void test_dsw2(void) {
  TEST_ASSERT_EQUAL_HEX8(0, spacduel_in1(5, 0, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(0, spacduel_in1(6, 0, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B6, spacduel_in1(7, 0, SD_SELECTED));
}

// the game ignores START until Select was pressed once after power up:
// START presses Select too while that is pending, never after
static void test_select(void) {
  TEST_ASSERT_EQUAL_HEX8(B7, spacduel_in1(6, BUTTON_START, SD_SELECT_PENDING));
  TEST_ASSERT_EQUAL_HEX8(B6, spacduel_in1(4, BUTTON_START, SD_SELECT_PENDING));
  TEST_ASSERT_EQUAL_HEX8(0, spacduel_in1(6, 0, SD_SELECT_PENDING));
  TEST_ASSERT_EQUAL_HEX8(0, spacduel_in1(6, BUTTON_START, SD_SELECTED));
}

// the stick moves the selection on the game select screen; the game
// ignores Select during play
static void test_select_stick(void) {
  TEST_ASSERT_EQUAL_HEX8(B7, spacduel_in1(6, BUTTON_DOWN, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B7, spacduel_in1(6, BUTTON_UP, SD_SELECTED));
  TEST_ASSERT_EQUAL_HEX8(B7, spacduel_in1(6, BUTTON_DOWN, SD_SELECT_PENDING));
  TEST_ASSERT_EQUAL_HEX8(0, spacduel_in1(6, BUTTON_LEFT | BUTTON_RIGHT | BUTTON_FIRE, SD_SELECTED));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_player1);
  RUN_TEST(test_dsw2);
  RUN_TEST(test_select);
  RUN_TEST(test_select_stick);
  return UNITY_END();
}
