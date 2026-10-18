#include <string.h>
#include <unity.h>

#include "../../src/machines/missile/missile_video.h"

using namespace missile_video;

void setUp(void) {}
void tearDown(void) {}

// MAME get_bit3_addr: line 224 -> 0x200, line 255 -> 0x5c1 (odd bytes 0x401-0x5ff)
static void test_bit3_addr(void) {
  TEST_ASSERT_EQUAL_HEX16(0x200, bit3Addr(224 << 8));
  TEST_ASSERT_EQUAL_HEX16(0x5c1, bit3Addr(255 << 8));
  TEST_ASSERT_EQUAL_HEX16(0x5c1 + 2 * 31, bit3Addr((255 << 8) | 0xf8));
}

static void test_mad_write_2bit(void) {
  uint8_t vram[0x4000] = {};
  uint8_t wprom[32];
  memset(wprom, 0xff, sizeof(wprom));
  wprom[0x10 | 1] = (uint8_t)~0x22; // pixel 1: bits 1 and 5
  madWrite(vram, wprom, 0x1001, 0x80); // data b7 = 1 -> lookup 0xf0
  TEST_ASSERT_EQUAL_HEX8(0x20, vram[0x1001 >> 2]);
  TEST_ASSERT_FALSE(madExtraCycle(0x1001));
}

static void test_mad_write_3bit(void) {
  uint8_t vram[0x4000] = {};
  uint8_t wprom[32];
  memset(wprom, 0xff, sizeof(wprom));
  wprom[0x18 | 3] = (uint8_t)~0x08; // 3rd bit plane, pixel 3
  madWrite(vram, wprom, 0xe003, 0x20); // data b5 = 1
  TEST_ASSERT_EQUAL_HEX8(0x08, vram[bit3Addr(0xe003)]);
  TEST_ASSERT_TRUE(madExtraCycle(0xe003));
}

static void test_mad_read(void) {
  uint8_t vram[0x4000] = {};
  vram[0x1000 >> 2] = 0x10; // pixel 0: high bit set, low clear
  TEST_ASSERT_EQUAL_HEX8(0xbf, madRead(vram, 0x1000));
  vram[bit3Addr(0xe000)] = 0x00;
  TEST_ASSERT_EQUAL_HEX8(0x1f, madRead(vram, 0xe000)); // b7, b6, b5 clear
}

static void test_pixel(void) {
  uint8_t vram[0x4000] = {};
  vram[100 * LINE_BYTES + 2] = 0x11; // x = 8: bit 4 -> 4, bit 0 -> 2
  TEST_ASSERT_EQUAL_UINT8(6, pixel(vram, 8, 100));
  TEST_ASSERT_EQUAL_UINT8(0, pixel(vram, 9, 100));
  vram[240 * LINE_BYTES + 0] = 0x11;
  vram[bit3Addr(240 << 8)] = 0x01;
  TEST_ASSERT_EQUAL_UINT8(7, pixel(vram, 0, 240));
}

static void test_palette(void) {
  TEST_ASSERT_EQUAL_HEX16(0x0000, paletteColor(0x0e)); // all inverted bits set -> black
  TEST_ASSERT_EQUAL_HEX16(0xffff, paletteColor(0x00)); // white
  TEST_ASSERT_EQUAL_HEX16(0x00f8, paletteColor(0x06)); // red 0xf800, bytes swapped
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_bit3_addr);
  RUN_TEST(test_mad_write_2bit);
  RUN_TEST(test_mad_write_3bit);
  RUN_TEST(test_mad_read);
  RUN_TEST(test_pixel);
  RUN_TEST(test_palette);
  return UNITY_END();
}
