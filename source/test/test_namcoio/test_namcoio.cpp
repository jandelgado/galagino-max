#include <unity.h>

#include "../../src/machines/namcoio.h"

// expected values from the boot-check comments in MAME namco/namcoio.cpp

static NamcoIO io;
static uint8_t port[4];   // active-low 4-bit inputs
static uint8_t sel_log[4];
static uint8_t sel_count;

static auto in = [](uint8_t p) -> uint8_t { return port[p & 3]; };
static auto sel = [](uint8_t v) {
  if (sel_count < 4) {
    sel_log[sel_count++] = v;
  }
};

void setUp(void) {
  io.reset();
  for (int i = 0; i < 4; i++) {
    port[i] = 0x0F;
  }
  sel_count = 0;
}
void tearDown(void) {}

static void set_args(const uint8_t *v) {
  for (int i = 0; i < 7; i++) {
    io.write(9 + i, v[i]);
  }
}

// 58XX mode 5, phozon: 9-15 = 0 1 2 3 4 5 6, expects 0-7 = 0 2 3 4 5 6 c a
static void test_58xx_bootcheck_phozon(void) {
  static const uint8_t args[7] = { 0, 1, 2, 3, 4, 5, 6 };
  static const uint8_t want[8] = { 0x0, 0x2, 0x3, 0x4, 0x5, 0x6, 0xc, 0xa };
  set_args(args);
  io.write(8, 5);
  io.run58(in, sel);
  for (int i = 0; i < 8; i++) {
    TEST_ASSERT_EQUAL_HEX8(0xF0 | want[i], io.read(i));
  }
}

// 58XX mode 5, mappy: 9-15 = 3 6 5 f a c e, expects 1-7 = 8 4 6 e d 9 d
static void test_58xx_bootcheck_mappy(void) {
  static const uint8_t args[7] = { 3, 6, 5, 0xf, 0xa, 0xc, 0xe };
  static const uint8_t want[7] = { 0x8, 0x4, 0x6, 0xe, 0xd, 0x9, 0xd };
  set_args(args);
  io.write(8, 5);
  io.run58(in, sel);
  for (int i = 0; i < 7; i++) {
    TEST_ASSERT_EQUAL_HEX8(0xF0 | want[i], io.read(1 + i));
  }
}

// 56XX mode 8, phozon: 9-15 = 1 2 3 4 5 6 7, expects 0-1 = 1 c
static void test_56xx_bootcheck_phozon(void) {
  static const uint8_t args[7] = { 1, 2, 3, 4, 5, 6, 7 };
  set_args(args);
  io.write(8, 8);
  io.run56(in, sel);
  TEST_ASSERT_EQUAL_HEX8(0xF1, io.read(0));
  TEST_ASSERT_EQUAL_HEX8(0xFC, io.read(1));
}

// 58XX credit mode: coin edge adds a credit, START edge spends it
static void test_58xx_coin_then_start(void) {
  io.write(8, 2);                       // coinage 1 coin / 1 credit
  io.write(9, 1); io.write(10, 1); io.write(11, 1); io.write(12, 1);
  io.run58(in, sel);

  io.write(8, 3);
  io.write(9, 0);                       // start buttons enabled
  port[0] = 0x0E;                       // coin 1 pressed
  io.run58(in, sel);
  TEST_ASSERT_EQUAL_HEX8(0xF0, io.read(2));   // tens (58XX: swap=2)
  TEST_ASSERT_EQUAL_HEX8(0xF1, io.read(3));   // units
  TEST_ASSERT_EQUAL_HEX8(0xF1, io.read(0));   // credit_add

  port[0] = 0x0F;                       // coin released
  port[3] = 0x0B;                       // start 1 pressed
  io.run58(in, sel);
  TEST_ASSERT_EQUAL_HEX8(0xF0, io.read(3));   // credits back to 0
  TEST_ASSERT_EQUAL_HEX8(0xF1, io.read(1));   // credit_sub
}

// coin 2 without a full credit must not drop coin 1's credit (MAME)
static void test_58xx_two_coins_same_frame(void) {
  io.write(8, 2);                       // coin A 1/1, coin B 2 coins / 1 credit
  io.write(9, 1); io.write(10, 1); io.write(11, 2); io.write(12, 1);
  io.run58(in, sel);

  io.write(8, 3);
  io.write(9, 0);
  port[0] = 0x0C;                       // coin 1 + coin 2 pressed
  io.run58(in, sel);
  TEST_ASSERT_EQUAL_HEX8(0xF1, io.read(3));   // one credit from coin 1
}

// 58XX mode 4 / 56XX mode 9: LS157 select 0 then 1, nibbles even/odd
static void test_dipmux_read(void) {
  port[0] = 0x0A;
  io.write(8, 9);
  io.run56(in, sel);
  TEST_ASSERT_EQUAL_UINT8(2, sel_count);
  TEST_ASSERT_EQUAL_UINT8(0, sel_log[0]);
  TEST_ASSERT_EQUAL_UINT8(1, sel_log[1]);
  TEST_ASSERT_EQUAL_HEX8(0xF5, io.read(0));   // ~0xA & 0xF
  TEST_ASSERT_EQUAL_HEX8(0xF5, io.read(1));

  sel_count = 0;
  io.write(8, 4);
  io.run58(in, sel);
  TEST_ASSERT_EQUAL_UINT8(2, sel_count);
  TEST_ASSERT_EQUAL_HEX8(0xF5, io.read(0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_58xx_bootcheck_phozon);
  RUN_TEST(test_58xx_bootcheck_mappy);
  RUN_TEST(test_56xx_bootcheck_phozon);
  RUN_TEST(test_58xx_coin_then_start);
  RUN_TEST(test_58xx_two_coins_same_frame);
  RUN_TEST(test_dipmux_read);
  return UNITY_END();
}
