#include <unity.h>

#include "../../src/machines/gberet/gberet_hw.h"

using namespace gberet_hw;

void setUp(void) {}
void tearDown(void) {}

static void test_tick_rise(void) {
  TEST_ASSERT_EQUAL_UINT8(0x01, tickRise(0));
  TEST_ASSERT_EQUAL_UINT8(0x02, tickRise(1));
  TEST_ASSERT_EQUAL_UINT8(0x10, tickRise(15));
  TEST_ASSERT_EQUAL_UINT8(0x00, tickRise(255)); // bit 8 truncated, as MAME uint8_t
}

static void test_nmi_eight_per_frame(void) {
  int n = 0;
  for (int t = 0; t < TICKS_PER_FRAME; t++) n += nmiTick((uint8_t)t, 0x01);
  TEST_ASSERT_EQUAL_INT(8, n);
  TEST_ASSERT_FALSE(nmiTick(0, 0x06)); // masked
}

static void test_irq_every_32_ticks(void) {
  int n = 0;
  for (int t = 0; t < 256; t++) n += irqTick((uint8_t)t, 0x04); // gberet: mask b2, bit 4
  TEST_ASSERT_EQUAL_INT(8, n);
  TEST_ASSERT_TRUE(irqTick(15, 0x04));
  TEST_ASSERT_TRUE(irqTick(7, 0x02));   // mask b1: rise of bit 3
  TEST_ASSERT_FALSE(irqTick(15, 0x01)); // NMI bit only
}

static void test_tile_rows(void) {
  TEST_ASSERT_EQUAL_INT(2, tileRow(0));
  TEST_ASSERT_EQUAL_INT(29, tileRow(VIS_LINES - 1));
  TEST_ASSERT_EQUAL_INT(0, tileLine(0));
  TEST_ASSERT_EQUAL_INT(7, tileLine(VIS_LINES - 1));
}

static void test_row_scroll(void) {
  uint8_t ram[0x40] = {0};
  ram[5] = 0x34;
  ram[5 + 0x20] = 0x01;
  TEST_ASSERT_EQUAL_UINT16(0x134, rowScroll(ram, 5));
}

static void test_tile_x_wraps(void) {
  TEST_ASSERT_EQUAL_UINT16(8, tileX(0, 0));
  TEST_ASSERT_EQUAL_UINT16(247, tileX(OUT_WIDTH - 1, 0));
  TEST_ASSERT_EQUAL_UINT16(0, tileX(0, 0x1f8));
  TEST_ASSERT_EQUAL_UINT16(17, tileX(10, 0x1ff)); // 10 + 8 + 511 = 529 -> 17
  TEST_ASSERT_EQUAL_UINT16(8, tileX(0, 0x200)); // bits above 8 ignored
}

static void test_sprite_column(void) {
  TEST_ASSERT_EQUAL_INT(-8, spriteColumn(0, 0x00));
  TEST_ASSERT_EQUAL_INT(247, spriteColumn(255, 0x00));
  TEST_ASSERT_EQUAL_INT(-248, spriteColumn(0x10, 0x80)); // x bit 8: left of screen
  TEST_ASSERT_EQUAL_INT(-9, spriteColumn(0xff, 0x80));
  TEST_ASSERT_EQUAL_INT(-8, spriteColumn(0, 0x7f));       // other attr bits ignored
}

static void test_sprite_line(void) {
  TEST_ASSERT_EQUAL_INT(0, spriteLine(16));
  TEST_ASSERT_EQUAL_INT(-15, spriteLine(1));
  TEST_ASSERT_EQUAL_INT(239, spriteLine(255));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_tick_rise);
  RUN_TEST(test_nmi_eight_per_frame);
  RUN_TEST(test_irq_every_32_ticks);
  RUN_TEST(test_tile_rows);
  RUN_TEST(test_row_scroll);
  RUN_TEST(test_tile_x_wraps);
  RUN_TEST(test_sprite_column);
  RUN_TEST(test_sprite_line);
  return UNITY_END();
}
