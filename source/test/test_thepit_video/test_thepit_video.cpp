#include <unity.h>

#include "../../src/machines/thepit/thepit_video.h"

using namespace thepit_video;

void setUp(void) {}
void tearDown(void) {}

static void test_priority_fields(void) {
  TEST_ASSERT_EQUAL(5, charColor(0x75));
  TEST_ASSERT_EQUAL(7, bgColor(0x75));
  TEST_ASSERT_TRUE(isPriority(0x10));    // background 1, b7 clear
  TEST_ASSERT_FALSE(isPriority(0x90));   // b7 set: no priority
  TEST_ASSERT_FALSE(isPriority(0x07));   // background 0: never priority
}

// strip 2 = native column 0; slot 0 = galagino columns 0-7 = native Y 232-239 = tile row 29
static void test_tile_addr_no_scroll(void) {
  TEST_ASSERT_EQUAL(29 * 32 + 0, tileAddr(2, 0, 0));
  TEST_ASSERT_EQUAL(2 * 32 + 31, tileAddr(33, 27, 0));
}

static void test_tile_addr_scroll(void) {
  TEST_ASSERT_EQUAL(30 * 32 + 4, tileAddr(6, 0, 8));    // one tile row further
  TEST_ASSERT_EQUAL(30 * 32 + 4, tileAddr(6, 0, 13));   // sub-pixel part ignored
  TEST_ASSERT_EQUAL(0 * 32 + 4, tileAddr(6, 0, 24));    // wraps at 32 rows
}

// slot -1 is the partial tile left of slot 0 when scroll & 7 != 0
static void test_tile_addr_partial_slot(void) {
  TEST_ASSERT_EQUAL(30 * 32 + 0, tileAddr(2, -1, 3));
}

static void test_sprite_decode(void) {
  // y=0x80, code 5 + flipx, priority + color 3, x=0x50
  const uint8_t s[4] = {0x80, 0x45, 0x0b, 0x50};
  Sprite spr;

  TEST_ASSERT_TRUE(decodeSprite(s, 5, spr));
  TEST_ASSERT_EQUAL(224 - 112, spr.x);  // native y = 240 - 0x80 = 112
  TEST_ASSERT_EQUAL(0x51 + 16, spr.y);  // native x = 0x50 + 1
  TEST_ASSERT_EQUAL(5, spr.code);
  TEST_ASSERT_EQUAL(1, spr.flip);       // flipx -> rows reversed after rotation
  TEST_ASSERT_EQUAL(3, spr.color);
  TEST_ASSERT_EQUAL(1, spr.high);

  // sprites 0-3 are one native pixel lower = one column further left
  TEST_ASSERT_TRUE(decodeSprite(s, 2, spr));
  TEST_ASSERT_EQUAL(224 - 113, spr.x);
}

static void test_sprite_flipy(void) {
  const uint8_t s[4] = {0x80, 0x80, 0x00, 0x50};
  Sprite spr;
  TEST_ASSERT_TRUE(decodeSprite(s, 7, spr));
  TEST_ASSERT_EQUAL(2, spr.flip);       // flipy -> pixels reversed after rotation
  TEST_ASSERT_EQUAL(0, spr.high);
}

static void test_sprite_disabled(void) {
  const uint8_t no_y[4] = {0x00, 0x01, 0x00, 0x50};
  const uint8_t no_x[4] = {0x80, 0x01, 0x00, 0x00};
  Sprite spr;
  TEST_ASSERT_FALSE(decodeSprite(no_y, 7, spr));
  TEST_ASSERT_FALSE(decodeSprite(no_x, 7, spr));
}

// MAME clips sprites to native x >= 17 (galagino line >= 33)
static void test_sprite_hidden_in_clip_band(void) {
  const uint8_t s[4] = {0x80, 0x01, 0x00, 0x01};  // native x 2 -> lines 18-33
  const uint8_t u[4] = {0x80, 0x01, 0x00, 0xff};  // x = b3 + 1 wraps: native x 0 -> lines 16-31
  Sprite spr;
  TEST_ASSERT_TRUE(decodeSprite(s, 7, spr));      // line 33 still visible
  TEST_ASSERT_FALSE(decodeSprite(u, 7, spr));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_priority_fields);
  RUN_TEST(test_tile_addr_no_scroll);
  RUN_TEST(test_tile_addr_scroll);
  RUN_TEST(test_tile_addr_partial_slot);
  RUN_TEST(test_sprite_decode);
  RUN_TEST(test_sprite_flipy);
  RUN_TEST(test_sprite_disabled);
  RUN_TEST(test_sprite_hidden_in_clip_band);
  return UNITY_END();
}
