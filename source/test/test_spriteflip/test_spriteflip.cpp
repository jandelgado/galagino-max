#include <unity.h>

#include "../../src/emulation/spriteflip.h"

static const int SIZE = 16;

// Pseudo-random sprite, so every pixel position is distinguishable.
static uint8_t pixel(int row, int col, uint8_t mask) {
  return (uint8_t)((row * 7 + col * 13 + row * col) & mask);
}

static uint8_t flippedPixel(int row, int col, uint8_t flip, uint8_t mask) {
  int r = (flip & FLIP_ROWS) ? SIZE - 1 - row : row;
  int c = (flip & FLIP_PIXELS) ? SIZE - 1 - col : col;
  return pixel(r, c, mask);
}

void setUp(void) {}
void tearDown(void) {}

static void test_rev2bpp(void) {
  TEST_ASSERT_EQUAL_HEX32(0x40000000, rev2bpp(0x00000001));
  TEST_ASSERT_EQUAL_HEX32(0xC0000000, rev2bpp(0x00000003));
  TEST_ASSERT_EQUAL_HEX32(0x1B1B1B1B, rev2bpp(0xE4E4E4E4));
  TEST_ASSERT_EQUAL_HEX32(0x12345678, rev2bpp(rev2bpp(0x12345678)));
}

static void test_rev4bpp(void) {
  TEST_ASSERT_EQUAL_HEX32(0x87654321, rev4bpp(0x12345678));
  TEST_ASSERT_EQUAL_HEX32(0xF0000000, rev4bpp(0x0000000F));
}

static void test_rows2bpp(void) {
  uint32_t spr[SIZE];
  for (int r = 0; r < SIZE; r++) {
    spr[r] = 0;
    for (int c = 0; c < SIZE; c++) {
      spr[r] |= (uint32_t)pixel(r, c, 3) << (2 * c);
    }
  }

  // every flip, starting at every row (blitters skip clipped rows)
  for (uint8_t flip = 0; flip < 4; flip++) {
    for (int skip = 0; skip < SIZE; skip++) {
      SpriteRows2bpp rows(spr, flip);
      rows.skip(skip);
      for (int r = skip; r < SIZE; r++) {
        uint32_t v = rows.next();
        for (int c = 0; c < SIZE; c++) {
          TEST_ASSERT_EQUAL_UINT8(flippedPixel(r, c, flip, 3), (v >> (2 * c)) & 3);
        }
      }
    }
  }
}

static void test_rows4bpp(void) {
  uint32_t spr[2 * SIZE] = {0};
  for (int r = 0; r < SIZE; r++) {
    for (int c = 0; c < SIZE; c++) {
      spr[2 * r + (c >> 3)] |= (uint32_t)pixel(r, c, 15) << (4 * (c & 7));
    }
  }

  for (uint8_t flip = 0; flip < 4; flip++) {
    for (int skip = 0; skip < SIZE; skip++) {
      SpriteRows4bpp rows(spr, flip);
      rows.skip(skip);
      for (int r = skip; r < SIZE; r++) {
        uint32_t v[2];
        rows.next(v);
        for (int c = 0; c < SIZE; c++) {
          TEST_ASSERT_EQUAL_UINT8(flippedPixel(r, c, flip, 15), (v[c >> 3] >> (4 * (c & 7))) & 15);
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_rev2bpp);
  RUN_TEST(test_rev4bpp);
  RUN_TEST(test_rows2bpp);
  RUN_TEST(test_rows4bpp);
  return UNITY_END();
}
