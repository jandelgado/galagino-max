#include <unity.h>

#include "../../src/machines/gorf/gorf_video.h"

using namespace gorf_video;

void setUp(void) {}
void tearDown(void) {}

static void test_row_map(void) {
  uint16_t src[OUT_H];
  uint8_t merge[OUT_H];
  buildRowMap(src, merge);
  TEST_ASSERT_EQUAL_UINT16(0, src[0]);
  TEST_ASSERT_EQUAL_INT(GAME_ROWS - 1, src[OUT_H - 1] + merge[OUT_H - 1]);
  static uint8_t covered[GAME_ROWS];
  int merges = 0;
  for (int r = 0; r < OUT_H; r++) {
    if (r > 0) TEST_ASSERT_TRUE(src[r] > src[r - 1]);
    covered[src[r]] = 1;
    if (merge[r]) {
      covered[src[r] + 1] = 1;
      merges++;
    }
  }
  TEST_ASSERT_EQUAL_INT(32, merges);
  for (int g = 0; g < GAME_ROWS; g++) TEST_ASSERT_EQUAL_UINT8(1, covered[g]);
}

// hand-made VRAM: one set pixel in game row 5 (p = 314 -> byte 78, bits 3-2) and
// one in game row 7 (p = 312 -> byte 78, bits 7-6), line 22 (first active column)
static void test_render_row_sanity(void) {
  static uint8_t vram[LINES * BYTES];
  uint16_t pal[512];
  buildPalette(pal);
  vram[78] = 0x40 | 0x08;  // p 312 = 1, p 314 = 2
  Regs r = {};
  r.colors[0] = 0x08;      // pix 0 right of split
  r.colors[1] = 0x38;      // pix 1
  r.colors[2] = 0x58;      // pix 2
  r.colors[4] = 0x88;      // pix 0 left of split
  r.colors[5] = 0xa8;
  r.colors[6] = 0xc8;
  r.vblank = LINES;
  r.sparkle[0] = r.sparkle[1] = r.sparkle[2] = r.sparkle[3] = 1;
  uint16_t row[OUT_W];

  renderRow(vram, r, 5, -1, pal, 0, row);
  TEST_ASSERT_EQUAL_HEX16(pal[0x58 << 1], row[TOP]);
  TEST_ASSERT_EQUAL_HEX16(pal[0x08 << 1], row[TOP + 1]);  // pix 0, active
  TEST_ASSERT_EQUAL_HEX16(pal[0x08 << 1], row[0]);        // border: bg = 0 -> pix 0
  TEST_ASSERT_EQUAL_HEX16(pal[0x08 << 1], row[TOP + LINES]);

  // merged pair (rows 5, 7): row 5 has pix 2 at line 22, row 7 pix 1 at line 22.
  // Pair (6, 7): row 6 empty, row 7 supplies pix 1.
  renderRow(vram, r, 6, 7, pal, 0, row);
  TEST_ASSERT_EQUAL_HEX16(pal[0x38 << 1], row[TOP]);

  // colorsplit: byte 78 left of split 80 -> colors 4..7
  r.split = 80;
  renderRow(vram, r, 5, -1, pal, 0, row);
  TEST_ASSERT_EQUAL_HEX16(pal[0xc8 << 1], row[TOP]);
  TEST_ASSERT_EQUAL_HEX16(pal[0x88 << 1], row[TOP + 1]);

  // sparkle on pixel 0: black
  r.split = 0;
  r.sparkle[0] = 0;
  renderRow(vram, r, 5, -1, pal, 0, row);
  TEST_ASSERT_EQUAL_HEX16(pal[0], row[TOP + 1]);
}

static void test_stars(void) {
  const uint16_t n = starCount();
  TEST_ASSERT_TRUE(n > 200 && n < 600);
  static uint32_t stars[600];
  buildStars(stars);
  for (int i = 1; i < n; i++) TEST_ASSERT_TRUE(stars[i] > stars[i - 1]);
  TEST_ASSERT_TRUE((stars[n - 1] >> 8) < GAME_ROWS && (stars[n - 1] & 0xff) < OUT_W);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_row_map);
  RUN_TEST(test_render_row_sanity);
  RUN_TEST(test_stars);
  return UNITY_END();
}
