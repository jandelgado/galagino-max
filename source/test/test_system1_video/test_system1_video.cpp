#include <unity.h>

#include "../../src/machines/system1/system1_video.h"

using namespace system1_video;

static uint8_t bank[SPRITE_BANK];

void setUp(void) { memset(bank, 0, sizeof(bank)); }
void tearDown(void) {}

// ROT270: row = 271 - x for both visareas, x0 is the bottom row
static void test_visarea_rows(void) {
  TEST_ASSERT_EQUAL(263, rowOfX(VISAREA_SCROLL.x0));
  TEST_ASSERT_EQUAL(24, rowOfX(VISAREA_SCROLL.x1));
  TEST_ASSERT_EQUAL(271, rowOfX(VISAREA_FULL.x0));
  TEST_ASSERT_EQUAL(16, rowOfX(VISAREA_FULL.x1));
  TEST_ASSERT_EQUAL(100, xOfRow(rowOfX(100)));
}

static void test_scroll_regs(void) {
  TEST_ASSERT_EQUAL(0x123 + 28, bgScroll(0x23, 0x01));
  TEST_ASSERT_EQUAL(-1 + 28, bgScroll(0xff, 0xff));
}

// MAME: ((x - scroll) / 2) & 0x1ff, C division truncates toward zero
static void test_bg_col_negative(void) {
  TEST_ASSERT_EQUAL(0, bgCol(16, 15));
  TEST_ASSERT_EQUAL(0, bgCol(16, 17));
  TEST_ASSERT_EQUAL(255, bgCol(16, 19));
  TEST_ASSERT_EQUAL(10, bgCol(532, 0));
}

static void test_tile_decode(void) {
  const uint8_t page[4] = {0, 0, 0x34, 0x92};
  const Tile t = tileAt(page, 1);
  TEST_ASSERT_EQUAL_HEX16(0xa34, t.code);
  TEST_ASSERT_EQUAL_HEX8(0x91, t.color);
  TEST_ASSERT_EQUAL_HEX16(0x8d, tilePen(t.color, 5));
}

static void test_mix_index(void) {
  // sprite transparent, fg transparent prio 2, bg pixel 3 prio 1
  TEST_ASSERT_EQUAL_HEX8(0x2b, mixIndex(false, 0, 0x80, 3, 0x40));
  TEST_ASSERT_EQUAL_HEX8(0x10, mixIndex(true, 1, 0x00, 0, 0x00));
}

static void test_tile_pixel(void) {
  uint32_t tiles[2][8] = {};
  tiles[1][3] = 5u << 3;  // column 3, row 6 = pen 5
  uint8_t page[PAGE_BYTES] = {};
  page[2 * (1 * PAGE_COLS + 2)] = 0x21;  // tile (col 2, row 1): code 0x21 & mask = 1, color 1
  uint8_t color = 0;
  TEST_ASSERT_EQUAL(5, tilePixel(page, tiles, 1, 2 * 8 + 3, 1 * 8 + 6, color));
  TEST_ASSERT_EQUAL(1, color);
}

static void test_decode_sprites(void) {
  uint8_t ram[SPRITES * SPRITE_BYTES] = {};
  const uint8_t e[SPRITE_BYTES] = {0x0f, 0x1f, 0x34, 0xe1, 0x20, 0x00, 0x00, 0x40};
  memcpy(ram, e, sizeof(e));
  ram[SPRITE_BYTES] = 0xff;  // ends the list
  Sprite s[SPRITES];
  TEST_ASSERT_EQUAL(1, decodeSprites(ram, 8, s));
  TEST_ASSERT_EQUAL(16, s[0].top);
  TEST_ASSERT_EQUAL(32, s[0].bottom);
  TEST_ASSERT_EQUAL(0x134, s[0].x);
  TEST_ASSERT_EQUAL(7, s[0].bank);
  TEST_ASSERT_EQUAL_HEX16(0x4020, rowAddr(s[0], 16));
  TEST_ASSERT_EQUAL_HEX16(0x4040, rowAddr(s[0], 17));
  TEST_ASSERT_EQUAL(0x9a, spriteX(0x134));
  TEST_ASSERT_EQUAL(0, decodeSprites(ram + SPRITE_BYTES, 8, s));
}

struct Px { int n; uint8_t c[300]; };

static void test_walk_row_forward(void) {
  bank[0x10] = 0x12;
  bank[0x11] = 0x3f;
  Px p = {};
  TEST_ASSERT_EQUAL(3, walkRow(bank, 0x0010, [&](int i, uint8_t c) { p.c[i] = c; p.n++; }));
  TEST_ASSERT_EQUAL(3, p.n);
  TEST_ASSERT_EQUAL(1, p.c[0]);
  TEST_ASSERT_EQUAL(3, p.c[2]);
}

// address bit 15: walk backwards, low nibble first
static void test_walk_row_mirrored(void) {
  bank[0x11] = 0x21;
  bank[0x10] = 0xf3;
  Px p = {};
  TEST_ASSERT_EQUAL(3, walkRow(bank, 0x8011, [&](int i, uint8_t c) { p.c[i] = c; }));
  TEST_ASSERT_EQUAL(1, p.c[0]);
  TEST_ASSERT_EQUAL(2, p.c[1]);
  TEST_ASSERT_EQUAL(3, p.c[2]);
}

static void test_walk_row_unterminated(void) {
  memset(bank, 0x11, sizeof(bank));
  TEST_ASSERT_EQUAL(2 * MAX_ROW_BYTES, walkRow(bank, 0x0000, [](int, uint8_t) {}));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_visarea_rows);
  RUN_TEST(test_scroll_regs);
  RUN_TEST(test_bg_col_negative);
  RUN_TEST(test_tile_decode);
  RUN_TEST(test_mix_index);
  RUN_TEST(test_tile_pixel);
  RUN_TEST(test_decode_sprites);
  RUN_TEST(test_walk_row_forward);
  RUN_TEST(test_walk_row_mirrored);
  RUN_TEST(test_walk_row_unterminated);
  return UNITY_END();
}
