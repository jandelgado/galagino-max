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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_vctr_stat);
  RUN_TEST(test_scale);
  RUN_TEST(test_svec);
  RUN_TEST(test_jsrl_rtsl);
  RUN_TEST(test_jmpl);
  RUN_TEST(test_clip_window);
  RUN_TEST(test_first_frame_not_clipped);
  RUN_TEST(test_loop_terminates);
  return UNITY_END();
}
