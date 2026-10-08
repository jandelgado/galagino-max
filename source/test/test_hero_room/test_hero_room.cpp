#include <stdio.h>
#include <unity.h>

#define ENABLE_HERO
#include "../../src/machines/hero/hero_data.cpp"
#include "../../src/machines/hero/hero_room.cpp"
#include "../../src/machines/hero/hero_video.cpp"
#include "hero_cmap_check.h"
#include "hero_mame_bands.h"

using namespace hero_room;
using hero_video::Frame;

void setUp(void) {}
void tearDown(void) {}

static Frame frameFor(uint8_t level, uint8_t screen) {
  Frame f = {};
  f.v.room = decode(level, screen);
  f.v.level = level;
  f.v.screen = screen;
  hero_video::prepare(f);
  return f;
}

static void test_level_sizes(void) {
  static const uint8_t expected[hero_data::LEVELS] = {2, 4, 6, 8, 8, 10, 12, 14, 16, 16,
                                                      16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
  int total = 0;
  for (int l = 0; l < hero_data::LEVELS; l++) {
    TEST_ASSERT_EQUAL_UINT8(expected[l], hero_data::screens(l));
    TEST_ASSERT_EQUAL_UINT8(expected[l] - 1, hero_data::minerScreen(l));
    total += hero_data::screens(l);
  }
  TEST_ASSERT_EQUAL(256, total);
}

// every band of every room against MAME (fixture from tools/roomcheck.py)
static void test_rooms_match_mame(void) {
  int room = 0;
  for (int l = 0; l < hero_data::LEVELS; l++) {
    for (int s = 0; s < hero_data::screens(l); s++, room++) {
      const Frame f = frameFor(l, s);
      for (int b = 0; b < BANDS; b++) {
        const uint64_t known = hero_mame_known[room][b];
        char msg[48];
        snprintf(msg, sizeof(msg), "level %d screen %d band %d", l + 1, s, b);
        TEST_ASSERT_EQUAL_HEX64_MESSAGE(hero_mame_cells[room][b] & known, f.cells[b] & known, msg);
      }
    }
  }
}

static void test_cells_mirror_and_repeat(void) {
  const Band mirror = {0x10, 0x00, 0x00, 0};
  TEST_ASSERT_EQUAL_HEX64(1ull | (1ull << 39), cells(mirror));
  const Band repeat = {0x10, 0x00, 0x00, 1};
  TEST_ASSERT_EQUAL_HEX64(1ull | (1ull << 20), cells(repeat));
}

// screen 0: start shaft wall at x 60..67 ($A0 = $3D); FE $08..$0B / $98..$9B: edge wall
static void test_ball(void) {
  TEST_ASSERT_EQUAL_UINT8(60, decode(0, 0).ball_x);
  TEST_ASSERT_EQUAL_UINT8(8, decode(0, 1).ball_x);       // room 1, FE $08 (miner)
  TEST_ASSERT_EQUAL_UINT8(NO_BALL, decode(1, 1).ball_x); // room 3, FE $34
  TEST_ASSERT_EQUAL_UINT8(8, decode(14, 15).ball_x);    // room 175, FE $08
  TEST_ASSERT_EQUAL_UINT8(152, decode(15, 2).ball_x);   // room 178, FE $9A
}

// colours seen in MAME, level 1 screen 1 (TIA colour >> 1)
static void test_line_colors(void) {
  const Frame f = frameFor(0, 1);
  uint8_t line[hero_video::SRC_W];
  hero_video::composeLine(f, hero_video::TOP_Y, line);
  TEST_ASSERT_EQUAL_HEX8(0x22 >> 1, line[20]);  // 612300
  hero_video::composeLine(f, hero_video::MID_Y - 1, line);
  TEST_ASSERT_EQUAL_HEX8(0x20 >> 1, line[20]);  // dark edge 3c0000
  hero_video::composeLine(f, hero_video::MID_Y + 1, line);
  TEST_ASSERT_EQUAL_HEX8(0x24 >> 1, line[20]);  // 854a00
  hero_video::composeLine(f, hero_video::BOTTOM_Y + 1, line);
  TEST_ASSERT_EQUAL_HEX8(0x26 >> 1, line[20]);  // a86e1a
  hero_video::composeLine(f, hero_video::FLOOR_Y + 1, line);
  TEST_ASSERT_EQUAL_HEX8(0x84 >> 1, line[12]);  // water 334dbb shows through the wave
  TEST_ASSERT_EQUAL_HEX8(0x28 >> 1, line[20]);  // wave ca9242
  hero_video::composeLine(f, hero_video::HUD_Y, line);
  TEST_ASSERT_EQUAL_HEX8(0x04 >> 1, line[20]);  // 535353
}

static void test_step_wraps_levels(void) {
  uint8_t level = 0, screen = 1;
  step(level, screen, 1);
  TEST_ASSERT_EQUAL_UINT8(1, level);
  TEST_ASSERT_EQUAL_UINT8(0, screen);
  step(level, screen, -1);
  TEST_ASSERT_EQUAL_UINT8(0, level);
  TEST_ASSERT_EQUAL_UINT8(1, screen);
  level = 0;
  screen = 0;
  step(level, screen, -1);
  TEST_ASSERT_EQUAL_UINT8(19, level);
  TEST_ASSERT_EQUAL_UINT8(15, screen);
  step(level, screen, 1);
  TEST_ASSERT_EQUAL_UINT8(0, level);
  TEST_ASSERT_EQUAL_UINT8(0, screen);
}

static void test_palette(void) {
  for (int i = 0; i < HERO_CMAP_CHECKS; i++) {
    TEST_ASSERT_EQUAL_HEX16(hero_cmap_check[i][1], hero_palette[hero_cmap_check[i][0] >> 1]);
  }
}

static void test_scale(void) {
  uint8_t src[hero_video::SRC_W];
  for (int i = 0; i < hero_video::SRC_W; i++) { src[i] = i & 0x7f; }
  uint16_t dst[hero_video::OUT_W];
  hero_video::scaleLine(src, dst);
  TEST_ASSERT_EQUAL_HEX16(hero_palette[0], dst[0]);
  TEST_ASSERT_EQUAL_HEX16(hero_palette[0], dst[1]);
  TEST_ASSERT_EQUAL_HEX16(hero_palette[1], dst[2]);
  TEST_ASSERT_EQUAL_HEX16(hero_palette[159 & 0x7f], dst[239]);
  TEST_ASSERT_EQUAL(191, hero_video::sourceLine(287));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_level_sizes);
  RUN_TEST(test_rooms_match_mame);
  RUN_TEST(test_cells_mirror_and_repeat);
  RUN_TEST(test_ball);
  RUN_TEST(test_line_colors);
  RUN_TEST(test_step_wraps_levels);
  RUN_TEST(test_palette);
  RUN_TEST(test_scale);
  return UNITY_END();
}
