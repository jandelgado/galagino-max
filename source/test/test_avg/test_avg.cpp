#include <string.h>
#include <unity.h>

#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/avg.cpp"

using vec2d::Line;

static uint8_t ram[0x200];
static Line out[64];
static avg::State st;

static const int32_t XC = 290 << 16, YC = 200 << 16;

void setUp(void) {
  memset(ram, 0, sizeof(ram));
  memset(out, 0x55, sizeof(out));
  st.reset(XC, YC);
}
void tearDown(void) {}

static avg::Memory mem() { return avg::Memory{ram, sizeof(ram), nullptr, 0, 0}; }

static uint16_t pc;
static void put(uint16_t w) {
  ram[pc * 2] = w & 0xff;
  ram[pc * 2 + 1] = w >> 8;
  pc++;
}
static void at(uint16_t word) { pc = word; }

static const uint16_t HALT = 0x2000, CNTR = 0x8000, RTSL = 0xc000;
static void vctr(int dx, int dy, int i) { put(dy & 0x1fff); put((i << 13) | (dx & 0x1fff)); }
static uint16_t scal(int b, int l) { return 0x7000 | (b << 8) | l; }
static uint16_t stat(int i, int clip = 0x400) { return 0x6000 | clip | (i << 4); }
static uint16_t svec(int dx, int dy, int i) { return 0x4000 | ((dy & 0x1f) << 8) | (i << 5) | (dx & 0x1f); }

static void assert_line(const Line &l, int x0, int y0, int x1, int y1, int color) {
  TEST_ASSERT_EQUAL(x0, l.x0);
  TEST_ASSERT_EQUAL(y0, l.y0);
  TEST_ASSERT_EQUAL(x1, l.x1);
  TEST_ASSERT_EQUAL(y1, l.y1);
  TEST_ASSERT_EQUAL(color, l.color);
}

// VCTR intensity field 1 takes the STAT intensity, others give field * 2
static void test_vctr_stat(void) {
  at(0); put(CNTR); put(scal(0, 0)); put(stat(12));
  vctr(100, 50, 1); vctr(-40, 0, 7); put(HALT);
  TEST_ASSERT_EQUAL(2, avg::decode(mem(), st, out, 64));
  assert_line(out[0], 290, 200, 389, 150, 12);
  assert_line(out[1], 389, 150, 349, 150, 14);
}

static void test_scale(void) {
  at(0); put(CNTR); put(scal(1, 0x80)); vctr(100, 50, 7); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(mem(), st, out, 64));
  assert_line(out[0], 290, 200, 314, 187, 14);
}

static void test_svec(void) {
  at(0); put(CNTR); put(scal(0, 0)); put(svec(4, -2, 7)); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(mem(), st, out, 64));
  assert_line(out[0], 290, 200, 297, 203, 14);
}

static void test_jsrl_rtsl(void) {
  at(0); put(CNTR); put(scal(0, 0)); put(0xa000 | 0x20); vctr(0, -30, 7); put(HALT);
  at(0x20); vctr(30, 0, 7); put(RTSL);
  TEST_ASSERT_EQUAL(2, avg::decode(mem(), st, out, 64));
  assert_line(out[0], 290, 200, 319, 200, 14);
  assert_line(out[1], 319, 200, 319, 229, 14);
}

static void test_jmpl(void) {
  at(0); put(CNTR); vctr(0, 0, 0); put(0xe000 | 0x10); vctr(10, 10, 7); put(HALT);
  at(0x10); vctr(0, 20, 7); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(mem(), st, out, 64));
  assert_line(out[0], 290, 200, 290, 180, 14);
}

// STAT bit 9 clear latches the window corner at the beam (lst), set
// latches the other corner (hst); the window applies to later lines
static void test_clip_window(void) {
  at(0); put(CNTR); put(scal(0, 0));
  vctr(-200, -100, 0); put(stat(10, 0x000));
  vctr(400, 200, 0); put(stat(10, 0x200));
  put(CNTR); vctr(-300, 0, 0); vctr(600, 0, 1); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(mem(), st, out, 64));
  assert_line(out[0], 90, 200, 489, 200, 10);
}

// first frame after reset: no clip record, default window keeps lines
static void test_first_frame_not_clipped(void) {
  at(0); put(CNTR); put(scal(0, 0)); vctr(100, 0, 7); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(mem(), st, out, 64));
}

// an endless JMPL loop ends after MAX_STEPS
static void test_loop_terminates(void) {
  at(0); put(0xe000);
  TEST_ASSERT_EQUAL(0, avg::decode(mem(), st, out, 64));
}

// glyph routine at word 0x40 draws 8 units and advances 4 more; 0x50 is
// not a glyph. A run survives STAT/SCAL and moves at caller level, CNTR
// and other JSRLs end it.
static void test_glyph_tagging(void) {
  at(0x40); vctr(8, 0, 7); vctr(4, 0, 0); put(RTSL);
  at(0x50); vctr(0, 10, 7); put(RTSL);
  at(0); put(CNTR); put(scal(0, 0)); vctr(-100, 0, 0);
  put(0xa040); put(scal(0, 0)); vctr(0, -20, 0); put(0xa040); // one run
  put(CNTR); put(0xa040);                                      // new run
  put(0xa050);                                                 // not a glyph
  put(0xa040);                                                 // new run
  put(HALT);
  const avg::Memory m{ram, sizeof(ram), nullptr, 0, 0, 0x80, 0xa0};
  TEST_ASSERT_EQUAL(5, avg::decode(m, st, out, 64));
  for (int i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL(i == 3 ? 14 : 14 | avg::GLYPH, out[i].color);
  }
  TEST_ASSERT_EQUAL(out[0].x0, out[0].dx);
  TEST_ASSERT_EQUAL(out[0].y0, out[0].dy);
  TEST_ASSERT_EQUAL(out[0].dx, out[1].dx);
  TEST_ASSERT_EQUAL(out[0].dy, out[1].dy);
  TEST_ASSERT_NOT_EQUAL(out[0].y0, out[1].y0);
  TEST_ASSERT_EQUAL(290, out[2].dx);
  TEST_ASSERT_EQUAL(200, out[2].dy);
  TEST_ASSERT_EQUAL(out[4].x0, out[4].dx);
  TEST_ASSERT_EQUAL(out[4].y0, out[4].dy);
  TEST_ASSERT_NOT_EQUAL(out[2].dy, out[4].dy);
}

// Star Wars: big endian words, STAT holds RGB (bits 10-8) and an 8 bit
// intensity scaled by the VCTR z field; black lines are skipped
static void test_starwars(void) {
  at(0); put(CNTR); put(scal(0, 0)); put(0x6380); vctr(100, 50, 7);
  put(0x6080); vctr(10, 0, 7); put(HALT);
  for (int a = 0; a < (int)sizeof(ram); a += 2) {
    const uint8_t t = ram[a];
    ram[a] = ram[a + 1];
    ram[a + 1] = t;
  }
  const avg::Memory m{ram, sizeof(ram), nullptr, 0, 0, 0, 0, avg::STARWARS};
  TEST_ASSERT_EQUAL(1, avg::decode(m, st, out, 64));
  // z 7: 7 * 0x80 / 8 = 112 -> level 7
  assert_line(out[0], 290, 200, 389, 150, (3 << avg::SW_COLOR_SHIFT) | 7);
}

// Tempest: STAT bit 11 selects color RAM index (bits 3-0) or intensity
// (bits 7-4); the line carries the color RAM value and level 1..7
static uint8_t colorram[16];
static avg::Memory tempest_mem() {
  return avg::Memory{ram, sizeof(ram), nullptr, 0, 0, 0, 0, avg::TEMPEST, colorram};
}
static uint16_t tp_color(int index) { return 0x6800 | index; }
static uint8_t tp_line(int value, int level) { return (value << avg::TP_COLOR_SHIFT) | level; }

static void test_tempest_color(void) {
  memset(colorram, 0, sizeof(colorram));
  colorram[3] = 0x6;
  at(0); put(CNTR); put(scal(0, 0)); put(tp_color(3)); put(stat(12));
  vctr(100, 50, 1); vctr(-40, 0, 7); put(HALT);
  TEST_ASSERT_EQUAL(2, avg::decode(tempest_mem(), st, out, 64));
  // intensity STAT keeps the color; level 12 -> 6, z 7 (level 14) -> 7
  assert_line(out[0], 290, 200, 389, 150, tp_line(6, 6));
  assert_line(out[1], 389, 150, 349, 150, tp_line(6, 7));
}

// color RAM value 0xf (all inverted bits set) is black: skipped
static void test_tempest_black(void) {
  memset(colorram, 0, sizeof(colorram));
  colorram[2] = 0xf;
  at(0); put(CNTR); put(scal(0, 0)); put(tp_color(2)); vctr(100, 0, 7);
  put(tp_color(1)); vctr(10, 0, 7); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(tempest_mem(), st, out, 64));
  assert_line(out[0], 389, 200, 399, 200, tp_line(0, 7));
}

// the Tempest AVG never halts: a frame ends at the JMPL back to 0
static void test_tempest_frame_end(void) {
  memset(colorram, 0, sizeof(colorram));
  at(0); put(0xe000 | 0x10);
  at(0x10); put(CNTR); put(scal(0, 0)); put(0xa000 | 0x20); put(0xe000);
  at(0x20); vctr(30, 0, 7); put(RTSL);
  TEST_ASSERT_EQUAL(1, avg::decode(tempest_mem(), st, out, 64));
}

// Tempest has no clip window: a STAT with bit 10 clear latches nothing
static void test_tempest_no_clip(void) {
  memset(colorram, 0, sizeof(colorram));
  at(0); put(CNTR); put(scal(0, 0));
  vctr(-200, -100, 0); put(stat(10, 0x000));
  vctr(400, 200, 0); put(stat(10, 0x200));
  put(CNTR); vctr(-300, 0, 0); vctr(600, 0, 1); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(tempest_mem(), st, out, 64));
  // bzone would clip to x 90..489 (test_clip_window); only the default
  // window (x >= 0) applies
  TEST_ASSERT_EQUAL(0, out[0].x0);
  TEST_ASSERT_GREATER_THAN(489, out[0].x1);
  TEST_ASSERT_EQUAL(tp_line(0, 5), out[0].color);
}

// Space Duel class AVG: STAT color (bits 2-0, color111) and intensity
// (bits 7-4); Line::color as Star Wars, RGB << SW_COLOR_SHIFT | level
static avg::Memory color_mem() {
  return avg::Memory{ram, sizeof(ram), nullptr, 0, 0, 0, 0, avg::COLOR, nullptr};
}
static uint8_t col_line(int rgb, int level) { return (rgb << avg::SW_COLOR_SHIFT) | level; }

static void test_color_stat(void) {
  at(0); put(CNTR); put(scal(0, 0)); put(stat(12) | 5);
  vctr(100, 50, 1); vctr(-40, 0, 7); put(HALT);
  TEST_ASSERT_EQUAL(2, avg::decode(color_mem(), st, out, 64));
  assert_line(out[0], 290, 200, 389, 150, col_line(5, 12));
  assert_line(out[1], 389, 150, 349, 150, col_line(5, 14));
}

// color111(0) is black: skipped like a move
static void test_color_black(void) {
  at(0); put(CNTR); put(scal(0, 0)); put(stat(12));
  vctr(100, 50, 7); put(stat(12) | 2); vctr(10, 0, 7); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(color_mem(), st, out, 64));
  assert_line(out[0], 389, 150, 399, 150, col_line(2, 14));
}

// no clip window: a STAT with bit 10 clear latches nothing
static void test_color_no_clip(void) {
  at(0); put(CNTR); put(scal(0, 0));
  vctr(-200, -100, 0); put(stat(10, 0x000) | 7);
  vctr(400, 200, 0); put(stat(10, 0x200) | 7);
  put(CNTR); vctr(-300, 0, 0); vctr(600, 0, 1); put(HALT);
  TEST_ASSERT_EQUAL(1, avg::decode(color_mem(), st, out, 64));
  TEST_ASSERT_EQUAL(0, out[0].x0);
  TEST_ASSERT_GREATER_THAN(489, out[0].x1);
  TEST_ASSERT_EQUAL(col_line(7, 10), out[0].color);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_vctr_stat);
  RUN_TEST(test_scale);
  RUN_TEST(test_svec);
  RUN_TEST(test_jsrl_rtsl);
  RUN_TEST(test_jmpl);
  RUN_TEST(test_starwars);
  RUN_TEST(test_clip_window);
  RUN_TEST(test_first_frame_not_clipped);
  RUN_TEST(test_loop_terminates);
  RUN_TEST(test_glyph_tagging);
  RUN_TEST(test_tempest_color);
  RUN_TEST(test_tempest_black);
  RUN_TEST(test_tempest_frame_end);
  RUN_TEST(test_tempest_no_clip);
  RUN_TEST(test_color_stat);
  RUN_TEST(test_color_black);
  RUN_TEST(test_color_no_clip);
  return UNITY_END();
}
