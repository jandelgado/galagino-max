#include <unity.h>
#include <string.h>

#include "../../src/machines/cbdash/cbdash_video.h"

using namespace cbdash_video;

static uint8_t charram[CHARRAM_SIZE];
static VideoState vs;
static uint16_t pal[PENS];
static uint16_t fb[OUT_WIDTH * 8];

void setUp(void) {
  memset(charram, 0, sizeof(charram));
  memset(&vs, 0, sizeof(vs));
  memset(fb, 0, sizeof(fb));
  for (int i = 0; i < PENS; i++) {
    pal[i] = 0x100 + i;
  }
}
void tearDown(void) {}

static uint16_t px(int k, int col) { return fb[k * OUT_WIDTH + col]; }

// RGB inverted: 0xff black, 0x00 white, 0xf8 = red only (byte swapped RGB565)
static void test_color565(void) {
  TEST_ASSERT_EQUAL_HEX16(0x0000, color565(0xff));
  TEST_ASSERT_EQUAL_HEX16(0xffff, color565(0x00));
  TEST_ASSERT_EQUAL_HEX16(0x00f8, color565(0xf8));
}

// tile columns are stored right to left, 32 tile rows each
static void test_fg_offset(void) {
  TEST_ASSERT_EQUAL_UINT16(31 * 32, fgOffset(0, 0));
  TEST_ASSERT_EQUAL_UINT16(5, fgOffset(31, 5));
}

// strip 20 = native tile column 13; native y 8..15 (tile row 1) = columns 0..7
static void test_char_pixels(void) {
  const int row = 20;
  const uint8_t tc = 33 - row;
  vs.fg[fgOffset(tc, 1)] = 1;
  charram[1 * 8 + 0] = 0x80;              // plane LSB, y 0, x 0 -> pen 1
  charram[2 * PLANE_SIZE + 1 * 8 + 3] = 0x01; // plane MSB, y 3, x 7 -> pen 4

  renderStrip(fb, row, charram, vs, pal, nullptr, 0);

  TEST_ASSERT_EQUAL_HEX16(pal[1], px(7, 0)); // x 0 is the strip's last line
  TEST_ASSERT_EQUAL_HEX16(pal[4], px(0, 3));
  TEST_ASSERT_EQUAL_HEX16(pal[BG_PEN], px(0, 0));
}

// ccb b0 selects the second char color, colorram b0-1 add 256 to the code
static void test_char_color_and_bank(void) {
  const int row = 20;
  const uint8_t tc = 33 - row;
  vs.ccb = 0x01;
  vs.fg[fgOffset(tc, 2)] = 0x05;
  vs.color[fgOffset(tc, 2)] = 0x01;
  charram[0x105 * 8 + 0] = 0x80;

  renderStrip(fb, row, charram, vs, pal, nullptr, 0);

  TEST_ASSERT_EQUAL_HEX16(pal[8 + 1], px(7, 8));
}

// strips outside the 256 native lines stay black
static void test_border_strips(void) {
  renderStrip(fb, 1, charram, vs, pal, nullptr, 0);
  TEST_ASSERT_EQUAL_HEX16(0, px(0, 0));
  renderStrip(fb, 34, charram, vs, pal, nullptr, 0);
  TEST_ASSERT_EQUAL_HEX16(0, px(7, 239));
}

// attr b0 enables; sx = 240 - x, sy = 240 - y; left half pixels at byte 16 + y
static void test_sprite(void) {
  const int row = 20;
  const int base = 8 * (33 - row);         // native x 104
  const int s = (7 - 2) * SPRITE_STRIDE;   // sprite slot 2
  vs.fg[s] = 0x01;
  vs.fg[s + SPRITE_CODE] = 2;
  vs.fg[s + SPRITE_Y] = 240 - 16;
  vs.fg[s + SPRITE_X] = 240 - base;
  charram[2 * 32 + 16] = 0x80;             // y 0, x 0, plane LSB -> pen 1

  Sprite spr[SPRITES];
  uint8_t n = collectSprites(vs, spr);
  TEST_ASSERT_EQUAL_UINT8(1, n);

  renderStrip(fb, row, charram, vs, pal, spr, n);
  TEST_ASSERT_EQUAL_HEX16(pal[1], px(7, 16 - VIS_Y0));
}

// a sprite above native y 0 wraps to the bottom (sy + 256) and is clipped
static void test_sprite_wrap(void) {
  const int row = 20;
  const int base = 8 * (33 - row);
  const int s = 0;                         // slot 7
  vs.fg[s] = 0x01;
  vs.fg[s + SPRITE_CODE] = 3;
  vs.fg[s + SPRITE_Y] = 250;               // sy = -10, wrapped 246
  vs.fg[s + SPRITE_X] = 240 - base;
  charram[3 * 32 + 16] = 0x80;             // y 0 -> native y 246
  charram[3 * 32 + 16 + 5] = 0x80;         // y 5 -> native y 251, invisible

  Sprite spr[SPRITES];
  uint8_t n = collectSprites(vs, spr);
  renderStrip(fb, row, charram, vs, pal, spr, n);

  TEST_ASSERT_EQUAL_HEX16(pal[1], px(7, 246 - VIS_Y0));
  for (int col = 0; col < OUT_WIDTH; col++) {
    TEST_ASSERT_NOT_EQUAL(0, px(7, col)); // every column written, none out of bounds
  }
}

// flip x mirrors within the 16 px: pixel x 0 lands on native x + 15
static void test_sprite_flip_x(void) {
  const int row = 20;
  const int base = 8 * (33 - row);
  const int s = 0;
  vs.fg[s] = 0x01 | 0x04;
  vs.fg[s + SPRITE_CODE] = 2;
  vs.fg[s + SPRITE_Y] = 240 - 16;
  vs.fg[s + SPRITE_X] = 240 - (base - 8);  // x 0 + flip -> native x base + 7
  charram[2 * 32 + 16] = 0x80;

  Sprite spr[SPRITES];
  uint8_t n = collectSprites(vs, spr);
  renderStrip(fb, row, charram, vs, pal, spr, n);

  TEST_ASSERT_EQUAL_HEX16(pal[1], px(0, 16 - VIS_Y0));
}

// flip y mirrors within the 16 lines: pixel y 0 lands on native y + 15
static void test_sprite_flip_y(void) {
  const int row = 20;
  const int base = 8 * (33 - row);
  const int s = 0;
  vs.fg[s] = 0x01 | 0x02;
  vs.fg[s + SPRITE_CODE] = 2;
  vs.fg[s + SPRITE_Y] = SPRITE_ORIGIN - 16;
  vs.fg[s + SPRITE_X] = SPRITE_ORIGIN - base;
  charram[2 * 32 + 16] = 0x80;             // y 0, x 0

  Sprite spr[SPRITES];
  uint8_t n = collectSprites(vs, spr);
  renderStrip(fb, row, charram, vs, pal, spr, n);

  TEST_ASSERT_EQUAL_HEX16(pal[1], px(7, 16 + 15 - VIS_Y0));
  TEST_ASSERT_EQUAL_HEX16(pal[BG_PEN], px(7, 16 - VIS_Y0));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_color565);
  RUN_TEST(test_fg_offset);
  RUN_TEST(test_char_pixels);
  RUN_TEST(test_char_color_and_bank);
  RUN_TEST(test_border_strips);
  RUN_TEST(test_sprite);
  RUN_TEST(test_sprite_wrap);
  RUN_TEST(test_sprite_flip_x);
  RUN_TEST(test_sprite_flip_y);
  return UNITY_END();
}
