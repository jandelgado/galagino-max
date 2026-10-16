#include <unity.h>

#include "../../src/machines/system1/system1_collision.cpp"

using namespace system1_video;

static uint8_t spriteRam[SPRITES * SPRITE_BYTES];
static uint8_t spriteRom[SPRITE_BANK];
static uint8_t vram[2 * PAGE_BYTES];
static uint32_t tiles[2][8];
static const uint16_t tileMap[2] = {0, 1};
static uint8_t prom[256];
static Collision col;

static const uint16_t ROW_DATA = 0x0100;  // 4 opaque pixels, then end
static const uint16_t FG_PAGE = PAGE_BYTES;
static const uint8_t COLLIDE_SPRITE_FG = 0x10;  // spr opaque, fg opaque prio 0, bg transparent

static void sprite(int n, uint8_t top, uint8_t lines, uint16_t x512) {
  uint8_t *e = spriteRam + n * SPRITE_BYTES;
  e[0] = top - 1;
  e[1] = top + lines - 1;
  e[2] = x512 & 0xff;
  e[3] = x512 >> 8;
  e[4] = 0;  // stride 0: every row reads ROW_DATA
  e[5] = 0;
  e[6] = ROW_DATA & 0xff;
  e[7] = ROW_DATA >> 8;
}

static Collision::Frame frame(uint8_t mode = 0, Visarea vis = VISAREA_SCROLL,
                              TileLayout layout = TILES_BY_COLUMN) {
  Collision::Frame f = {spriteRam, spriteRom, 1, vram, {tiles, tileMap, 1, layout}, prom, mode, vis};
  return f;
}

void setUp(void) {
  memset(spriteRam, 0, sizeof(spriteRam));
  memset(vram, 0, sizeof(vram));
  memset(prom, 0x0c, sizeof(prom));  // no collision anywhere
  prom[COLLIDE_SPRITE_FG] = 0x00;
  spriteRom[ROW_DATA] = 0x11;
  spriteRom[ROW_DATA + 1] = 0x11;
  spriteRom[ROW_DATA + 2] = 0xff;
  for (int c = 0; c < 8; c++) {
    tiles[1][c] = 0xffffff;
  }
  for (int n = 0; n < SPRITES; n++) {
    spriteRam[n * SPRITE_BYTES] = SPRITE_LIST_END;
  }
  col.reset();
}

void tearDown(void) {}

// sprite 1 drawn over sprite 0: bit [0 + 32 * 1]
static void test_sprite_pair(void) {
  sprite(0, 10, 2, 100);
  sprite(1, 10, 2, 104);
  col.scan(frame());
  TEST_ASSERT_EQUAL_HEX8(0xff, col.sprRead(0 + 32 * 1));
  TEST_ASSERT_EQUAL_HEX8(0xfe, col.sprRead(1 + 32 * 0));
  col.sprClear(0 + 32 * 1);
  col.sprReset();
  TEST_ASSERT_EQUAL_HEX8(0x7e, col.sprRead(0 + 32 * 1));
}

// fg tile under sprite 0 only; sprite 2 elsewhere
static void test_sprite_vs_fg(void) {
  sprite(0, 10, 2, 100);  // fg x 50..53, y 10..11 -> tile (6, 1)
  sprite(1, 100, 2, 300);
  vram[FG_PAGE + 2 * (1 * PAGE_COLS + 6)] = 1;
  col.scan(frame());
  TEST_ASSERT_EQUAL_HEX8(0xff, col.mixRead(0));
  TEST_ASSERT_EQUAL_HEX8(0xfe, col.mixRead(1));
  TEST_ASSERT_TRUE(col.hit());
  col.mixReset();
  TEST_ASSERT_EQUAL_HEX8(0x7f, col.mixRead(0));
}

static void test_list_end_marker(void) {
  sprite(0, 10, 2, 100);
  spriteRam[1 * SPRITE_BYTES] = SPRITE_LIST_END;
  sprite(2, 10, 2, 104);
  col.scan(frame());
  TEST_ASSERT_EQUAL_HEX8(0x7e, col.sprRead(0 + 32 * 2));
  TEST_ASSERT_FALSE(col.hit());
}

static void test_blank_no_hits(void) {
  sprite(0, 10, 2, 100);
  sprite(1, 10, 2, 104);
  col.scan(frame(VIDEO_BLANK));
  TEST_ASSERT_FALSE(col.hit());
}

// overlap left of x 16 and below line 223 is outside the cliprect
static void test_offscreen_ignored(void) {
  sprite(0, 10, 2, 4);   // covers 4..11
  sprite(1, 10, 2, 6);
  sprite(2, 230, 2, 200);
  sprite(3, 230, 2, 202);
  col.scan(frame());
  TEST_ASSERT_FALSE(col.hit());
}

// two sprites overlapping at 512 units 4..9: hidden by sys1ppis, visible on sys1ppi
static void test_collision_respects_visarea(void) {
  sprite(0, 10, 2, 2);
  sprite(1, 10, 2, 4);
  col.scan(frame(0, VISAREA_SCROLL));
  TEST_ASSERT_EQUAL_HEX8(0x7e, col.sprRead(0 + 32 * 1));
  col.reset();
  col.scan(frame(0, VISAREA_FULL));
  TEST_ASSERT_EQUAL_HEX8(0xff, col.sprRead(0 + 32 * 1));
}

// line buffer starts empty on every line: same x on the next line is no hit
static void test_line_buffer_per_line(void) {
  sprite(0, 10, 1, 100);
  sprite(1, 11, 1, 100);
  col.scan(frame());
  TEST_ASSERT_FALSE(col.hit());
}

// and on every frame
static void test_line_buffer_per_frame(void) {
  sprite(0, 10, 1, 100);
  col.scan(frame());
  sprite(0, 50, 1, 300);
  sprite(1, 10, 1, 100);
  col.scan(frame());
  TEST_ASSERT_FALSE(col.hit());
}

// fg tile 7 (x 56..63) under part of sprite 1 only: lookups follow each unit
static void test_fg_per_unit(void) {
  sprite(0, 10, 1, 96);   // fg x 48..51, tile 6
  sprite(1, 10, 1, 110);  // fg x 55..58, crosses into tile 7
  vram[FG_PAGE + 2 * (1 * PAGE_COLS + 7)] = 1;
  col.scan(frame());
  TEST_ASSERT_EQUAL_HEX8(0xfe, col.mixRead(0));  // summary bit 7 set by sprite 1
  TEST_ASSERT_EQUAL_HEX8(0xff, col.mixRead(1));
}

// overlap only at unit 32: sprite 1's pixel at 31..32 spans two line words
static void test_pixel_across_words(void) {
  sprite(0, 10, 1, 32);  // units 32..39
  sprite(1, 10, 1, 25);  // units 25..32
  col.scan(frame(0, VISAREA_FULL));
  TEST_ASSERT_EQUAL_HEX8(0xff, col.sprRead(0 + 32 * 1));
}

// PROM collides fg priority 2 only: same tile at priority 0 is no hit
static void test_fg_priority(void) {
  const uint8_t FG_PRI2_IDX = 0x18;  // spr opaque, fg opaque prio 2, bg transparent
  const uint8_t PRI2 = 2 << 3;       // tile word bits 11-12
  prom[COLLIDE_SPRITE_FG] = 0x0c;
  prom[FG_PRI2_IDX] = 0x00;
  sprite(0, 10, 1, 100);  // fg tile (6, 1)
  vram[FG_PAGE + 2 * (1 * PAGE_COLS + 6)] = 1;
  col.scan(frame());
  TEST_ASSERT_FALSE(col.hit());

  vram[FG_PAGE + 2 * (1 * PAGE_COLS + 6) + 1] = PRI2;
  col.scan(frame());
  TEST_ASSERT_EQUAL_HEX8(0xff, col.mixRead(0));
}

// ROT0 tiles stored by row: the one opaque pixel (x 51, y 10) collides
static void test_fg_tiles_by_row(void) {
  for (int c = 0; c < 8; c++) {
    tiles[1][c] = 0;
  }
  tiles[1][2] = 7u << (3 * (7 - 3));  // row 2, column 3
  sprite(0, 10, 1, 100);              // fg x 50..53 on line 10, tile (6, 1)
  vram[FG_PAGE + 2 * (1 * PAGE_COLS + 6)] = 1;
  col.scan(frame(0, VISAREA_SCROLL, TILES_BY_ROW));
  TEST_ASSERT_TRUE(col.hit());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sprite_pair);
  RUN_TEST(test_sprite_vs_fg);
  RUN_TEST(test_list_end_marker);
  RUN_TEST(test_blank_no_hits);
  RUN_TEST(test_offscreen_ignored);
  RUN_TEST(test_collision_respects_visarea);
  RUN_TEST(test_line_buffer_per_line);
  RUN_TEST(test_line_buffer_per_frame);
  RUN_TEST(test_fg_per_unit);
  RUN_TEST(test_pixel_across_words);
  RUN_TEST(test_fg_priority);
  RUN_TEST(test_fg_tiles_by_row);
  return UNITY_END();
}
