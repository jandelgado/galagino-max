#include <stdio.h>
#include <unity.h>

#define ENABLE_HERO
#include "../../src/machines/hero/hero_data.cpp"
#include "../../src/machines/hero/hero_room.cpp"
#include "../../src/machines/hero/hero_video.cpp"
#include "../../src/machines/hero/hero_game.cpp"
#include "hero_cmap_check.h"
#include "hero_mame_bands.h"

using namespace hero_room;
using hero_video::Frame;

void setUp(void) {}
void tearDown(void) {}

static Frame frameFor(uint8_t level, uint8_t screen) {
  static hero_game::Game g;
  hero_game::viewRoom(g, level, screen);
  Frame f = {};
  f.v = g.view;
  f.v.hero_page = hero_video::NO_HERO;
  for (int i = 0; i < 3; i++) { f.v.obj[i].x = f.v.obj2[i].x = hero_game::NONE; }
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

// every band of every room against MAME (fixture from romconv/dev/hero/roomcheck.py)
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

// screen 0: start wall at x 60..67 ($A0 = $3D); others: $A0 = (FE & $FC) | 1
static void test_ball(void) {
  TEST_ASSERT_EQUAL_UINT8(60, decode(0, 0).ball_x);
  TEST_ASSERT_EQUAL_UINT8(8, decode(0, 1).ball_x);       // room 1, FE $08 (miner)
  TEST_ASSERT_EQUAL_UINT8(52, decode(1, 1).ball_x);      // room 3, FE $34, mid-screen wall
  TEST_ASSERT_EQUAL_UINT8(NO_BALL, decode(1, 2).ball_x); // room 4, FE $01
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

// level 1 at y $6A, x $20, frame $DEB3: source line 25 (sprite row 10) is
// .######. in colour $99 (66a4e9)
static void test_hero_sprite_row(void) {
  Frame f = frameFor(0, 0);
  f.v.hero_x = 0x20;
  f.v.hero_y = 0x6a;
  f.v.hero_page = 0xde;
  f.v.hero_frame = 0xb3;
  uint8_t line[hero_video::SRC_W];
  hero_video::composeLine(f, 25, line);
  TEST_ASSERT_EQUAL_HEX8(0, line[0x20]);
  for (int x = 0x21; x <= 0x26; x++) { TEST_ASSERT_EQUAL_HEX8(0x98 >> 1, line[x]); }
  TEST_ASSERT_EQUAL_HEX8(0, line[0x27]);
  f.v.hero_mirror = 1; // REFP1
  hero_video::composeLine(f, 25 + 1, line); // row 11: .###.##. -> .##.###.
  TEST_ASSERT_EQUAL_HEX8(0, line[0x23]);
  TEST_ASSERT_EQUAL_HEX8(0x98 >> 1, line[0x24]);
}

// CTRLPF priority: walls cover the hero
static void test_hero_behind_playfield(void) {
  Frame f = frameFor(0, 0);
  f.v.hero_x = 0x10; // top band wall covers x 0..23
  f.v.hero_y = 0x6a;
  f.v.hero_page = 0xde;
  f.v.hero_frame = 0xb3;
  uint8_t line[hero_video::SRC_W];
  hero_video::composeLine(f, 25, line);
  TEST_ASSERT_EQUAL_HEX8(0x22 >> 1, line[0x12]);
}

static void test_palette(void) {
#ifdef HERO_NTSC_COLORS
  for (int i = 0; i < HERO_CMAP_CHECKS; i++) {
    TEST_ASSERT_EQUAL_HEX16(hero_cmap_check[i][1], hero_palette[hero_cmap_check[i][0] >> 1]);
  }
#else
  // NTSC colour -> its PAL cart colour (hue remapped) in Stella's PAL palette
  static const uint16_t pal_check[][2] = {
    {0x20, 0x0058}, // PAL 0x40, 590000
    {0x44, 0xe9a1}, // PAL 0x64, a73d4d
    {0x84, 0x372b}, // PAL 0xb4, 2d65bf
    {0xc4, 0x8214}, // PAL 0x54, 169216
    {0x04, 0xcb5a}, // grey, 595959
  };
  for (const auto &c : pal_check) { TEST_ASSERT_EQUAL_HEX16(c[1], hero_palette[c[0] >> 1]); }
#endif
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
#ifdef HERO_SCALE_Y
  TEST_ASSERT_EQUAL(191, hero_video::sourceLine(287));
#else
  TEST_ASSERT_EQUAL(-1, hero_video::sourceLine(47));
  TEST_ASSERT_EQUAL(0, hero_video::sourceLine(48));
  TEST_ASSERT_EQUAL(191, hero_video::sourceLine(239));
  TEST_ASSERT_EQUAL(192, hero_video::sourceLine(240));
#endif
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_level_sizes);
  RUN_TEST(test_rooms_match_mame);
  RUN_TEST(test_cells_mirror_and_repeat);
  RUN_TEST(test_ball);
  RUN_TEST(test_line_colors);
  RUN_TEST(test_step_wraps_levels);
  RUN_TEST(test_hero_sprite_row);
  RUN_TEST(test_hero_behind_playfield);
  RUN_TEST(test_palette);
  RUN_TEST(test_scale);
  return UNITY_END();
}
