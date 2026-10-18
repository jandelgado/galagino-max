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

// one emulated frame, a redraw published in the frame it started
static void publish(void) {
  static uint8_t draws = 0;
  vs.frame++;
  if (vs.draws != draws) {
    vs.draw_frame = vs.frame;
    draws = vs.draws;
  }
}

// one emulated frame, shown
static void update(Scroll &s, bool have_old) {
  publish();
  scrollUpdate(s, vs, have_old);
}

static Scroll steadyScroll(void) {
  Scroll s = {10, 5, 0, 0, 0, 0, DEFAULT_PERIOD, 0xff};
  vs.cam_x = 10;
  vs.cam_y = 5;
  for (int d = 1; d <= 3; d++) {  // draws every 11 frames, no camera change
    vs.draws = d;
    for (int f = 0; f < 11; f++) {
      update(s, false);
    }
  }
  return s;
}

// a one cell step after steady draws glides 16 px back to 0 over one period
static void test_scroll_glide(void) {
  Scroll s = steadyScroll();
  vs.draws++;
  vs.cam_x = 11;
  update(s, true);

  TEST_ASSERT_EQUAL_UINT8(11, s.period);
  ScrollView v = scrollView(s);
  TEST_ASSERT_EQUAL_INT8(-CELL, v.ox);
  TEST_ASSERT_EQUAL_INT8(0, v.oy);

  for (int f = 0; f < 10; f++) {
    update(s, false);
  }
  TEST_ASSERT_EQUAL_INT8(-1, scrollView(s).ox);
  update(s, false);
  TEST_ASSERT_EQUAL_INT8(0, scrollView(s).ox);
}

// cam_y steps glide along native x, opposite sign
static void test_scroll_glide_y(void) {
  Scroll s = steadyScroll();
  vs.draws++;
  vs.cam_y = 6;
  update(s, true);
  TEST_ASSERT_EQUAL_INT8(CELL, scrollView(s).oy);
}

// VIDEO_HALF_RATE: one display frame per 2 emulated frames, a tick of 11
// is 5.5 display frames. The glide still moves 2 or 3 px every frame.
static void test_scroll_glide_half_rate(void) {
  Scroll s = steadyScroll();
  int prev = vs.cam_x * CELL;
  for (int f = 1; f <= 6 * 11; f++) {
    if (f % 11 == 0) {
      vs.draws++;
      vs.cam_x++;
    }
    publish();
    if (f % 2) {
      continue;
    }

    scrollUpdate(s, vs, true);
    const int pos = vs.cam_x * CELL + scrollView(s).ox;
    if (f > 22) {  // period measured
      TEST_ASSERT_TRUE_MESSAGE(pos - prev == 2 || pos - prev == 3, "uneven step");
    }
    prev = pos;
  }
}

// paced ticks of TICK_FRAMES: 2 px every frame, 4 px every display frame
// at VIDEO_HALF_RATE
static void test_scroll_paced_even(void) {
  for (int rate = 1; rate <= 2; rate++) {
    Scroll s = steadyScroll();
    int prev = vs.cam_x * CELL;
    for (int f = 1; f <= 6 * TICK_FRAMES; f++) {
      if (f % TICK_FRAMES == 0) {
        vs.draws++;
        vs.cam_x++;
      }
      publish();
      if (f % rate) {
        continue;
      }

      scrollUpdate(s, vs, true);
      const int pos = vs.cam_x * CELL + scrollView(s).ox;
      if (f > 2 * TICK_FRAMES) {  // period measured
        TEST_ASSERT_EQUAL_INT_MESSAGE(2 * rate, pos - prev, "uneven step");
      }
      prev = pos;
    }
  }
}

// jumps, missing old view and draws after a pause snap
static void test_scroll_snap(void) {
  Scroll s = steadyScroll();
  vs.draws++;
  vs.cam_x = 12;
  update(s, true);
  TEST_ASSERT_EQUAL_INT8(0, scrollView(s).ox);

  s = steadyScroll();
  vs.draws++;
  vs.cam_x = 11;
  update(s, false);
  TEST_ASSERT_EQUAL_INT8(0, scrollView(s).ox);

  s = steadyScroll();
  for (int f = 0; f < 40; f++) {
    update(s, false);
  }
  vs.draws++;
  vs.cam_x = 11;
  update(s, true);
  TEST_ASSERT_EQUAL_INT8(0, scrollView(s).ox);
}

// cam_x +1, ox -16: output columns 0..15 show the old view's first cell
static void test_shifted_band_x(void) {
  static VideoState old;
  memset(&old, 0, sizeof(old));
  const int row = 20;
  const uint8_t tc = 33 - row;
  old.fg[fgOffset(tc, 1)] = 1;
  vs.fg[fgOffset(tc, 1)] = 2;
  charram[1 * 8 + 0] = 0x01;              // y 0, bit 0 -> line 0
  charram[2 * 8 + 0] = 0x01;
  charram[PLANE_SIZE + 2 * 8 + 0] = 0x01; // code 2: pen 3

  ScrollView v = {-CELL, 0, 1, 0};
  renderShifted(fb, row, charram, vs, old, v, pal);

  TEST_ASSERT_EQUAL_HEX16(pal[1], px(0, 0));
  TEST_ASSERT_EQUAL_HEX16(pal[3], px(0, 16));
  TEST_ASSERT_EQUAL_HEX16(pal[BG_PEN], px(0, 1));
}

// oy 4: lines 0..3 come from the tile column above (bits 4..7), 4..7 from
// the strip's own column (bits 0..3)
static void test_shifted_split_y(void) {
  static VideoState old;
  memset(&old, 0, sizeof(old));
  const int row = 20;
  const uint8_t tc = 33 - row;
  vs.fg[fgOffset(tc + 1, 1)] = 1;
  vs.fg[fgOffset(tc, 1)] = 2;
  charram[1 * 8 + 0] = 0x10;              // bit 4
  charram[PLANE_SIZE + 2 * 8 + 0] = 0x01; // bit 0, pen 2

  ScrollView v = {0, 4, 0, 0};
  renderShifted(fb, row, charram, vs, old, v, pal);

  TEST_ASSERT_EQUAL_HEX16(pal[1], px(0, 0));
  TEST_ASSERT_EQUAL_HEX16(pal[2], px(4, 0));
  TEST_ASSERT_EQUAL_HEX16(pal[BG_PEN], px(3, 0));
}

// MAME dump: the game draws the playfield to native cols 25..2 (strips
// 8..31), cols 0, 1 stay blank
static void test_playfield_window(void) {
  static VideoState old;
  memset(&old, 0, sizeof(old));
  old.fg[fgOffset(25, 1)] = 1;
  old.fg[fgOffset(2, 1)] = 1;
  charram[1 * 8 + 0] = 0x81; // lines 7 and 0

  TEST_ASSERT_EQUAL_INT(8, PF_FIRST_STRIP);
  TEST_ASSERT_EQUAL_INT(31, PF_LAST_STRIP);

  // cam_y +1: top strip shows the old top column
  ScrollView up = {0, CELL, 0, 1};
  renderShifted(fb, 8, charram, vs, old, up, pal);
  TEST_ASSERT_EQUAL_HEX16(pal[1], px(0, 0));

  // cam_y -1: bottom strip shows the old bottom column
  ScrollView down = {0, -CELL, 0, -1};
  renderShifted(fb, 31, charram, vs, old, down, pal);
  TEST_ASSERT_EQUAL_HEX16(pal[1], px(7, 0));
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
  RUN_TEST(test_scroll_glide);
  RUN_TEST(test_scroll_glide_y);
  RUN_TEST(test_scroll_glide_half_rate);
  RUN_TEST(test_scroll_paced_even);
  RUN_TEST(test_scroll_snap);
  RUN_TEST(test_shifted_band_x);
  RUN_TEST(test_shifted_split_y);
  RUN_TEST(test_playfield_window);
  return UNITY_END();
}
