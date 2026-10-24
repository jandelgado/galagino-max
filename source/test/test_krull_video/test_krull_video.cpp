#include <string.h>
#include <unity.h>

#include "../../src/machines/gottlieb/gottlieb_video.h"

void setUp(void) {}
void tearDown(void) {}

void test_char_write(void) {
  static uint32_t tiles[128][8];
  memset(tiles, 0, sizeof(tiles));
  const uint8_t bytes[4] = {0x12, 0x34, 0x56, 0x78};
  for (int i = 0; i < 4; i++) {
    gottlieb_video::charWrite(tiles, 3 * 32 + 2 * 4 + i, bytes[i]);
  }
  for (int x = 0; x < 8; x++) {
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(x + 1) << 8, tiles[3][x]);
  }
  gottlieb_video::charWrite(tiles, 3 * 32 + 8, 0x00);
  TEST_ASSERT_EQUAL_HEX32(0, (tiles[3][0] >> 8) & 0xf);
  TEST_ASSERT_EQUAL_HEX32(0, (tiles[3][1] >> 8) & 0xf);
  TEST_ASSERT_EQUAL_HEX32(3, (tiles[3][2] >> 8) & 0xf);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_char_write);
  return UNITY_END();
}
