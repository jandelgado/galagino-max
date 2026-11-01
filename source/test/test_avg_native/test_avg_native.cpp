#include <string.h>
#include <unity.h>

// avg::decode native paths (HDMI): sub-unit coordinates and sparkle split
// on synthetic lists.
#define HAL_VIDEO_NATIVE 1
#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/avg.cpp"

using vec2d::Line;

static const uint8_t FRAC = 4;
static uint8_t ram[0x200];
static uint8_t colorram[32];
static Line out[64];
static avg::State st;

static const int32_t XC = 290 << 16, YC = 200 << 16;

void setUp(void) {
  memset(ram, 0, sizeof(ram));
  memset(colorram, 0, sizeof(colorram));
  memset(out, 0x55, sizeof(out));
  st.reset(XC, YC);
}
void tearDown(void) {}

static avg::Memory mem(uint8_t frac, avg::Variant v = avg::BZONE, bool sparkle = false) {
  return avg::Memory{ram, sizeof(ram), nullptr, 0x1000, 0, 0, 0, v, colorram, nullptr, sparkle,
                     frac};
}

static uint16_t pc;
static void put(uint16_t w) {
  ram[pc * 2] = w & 0xff;
  ram[pc * 2 + 1] = w >> 8;
  pc++;
}

static const uint16_t HALT = 0x2000, CNTR = 0x8000;
static void vctr(int dx, int dy, int i) { put(dy & 0x1fff); put((i << 13) | (dx & 0x1fff)); }
static uint16_t scal(int b, int l) { return 0x7000 | (b << 8) | l; }
static uint16_t stat(int i) { return 0x6000 | 0x400 | (i << 4); }

// A vector from the center to beyond int16 at 1/16 units (2047 beam units)
// keeps its slope inside the window: the far endpoint must not saturate per
// axis before the clip (bent lines in Star Wars / Major Havoc 3D).
// Scale 0x80: about half the vector, scale 0 overflows the deflection.
static void test_far_endpoint_keeps_slope(void) {
  pc = 0;
  put(CNTR); put(scal(0, 0x80)); put(stat(12)); vctr(4000, 3000, 7); put(HALT);

  avg::State s = st;
  Line whole[4];
  TEST_ASSERT_EQUAL(1, avg::decode(mem(0), s, whole, 4));
  s = st;
  TEST_ASSERT_EQUAL(1, avg::decode(mem(FRAC), s, out, 64));
  TEST_ASSERT_GREATER_THAN(2047, s.xpos >> 16);

  // clipped at the window top (y 0), x about 290 + 4000 * 200 / 3000
  TEST_ASSERT_EQUAL(0, whole[0].y1);
  TEST_ASSERT_INT_WITHIN(2, 556, whole[0].x1);
  const Line &l = out[0];
  TEST_ASSERT_INT_WITHIN(16, whole[0].x0 << FRAC, l.x0);
  TEST_ASSERT_INT_WITHIN(16, whole[0].y0 << FRAC, l.y0);
  TEST_ASSERT_INT_WITHIN(16, whole[0].x1 << FRAC, l.x1);
  TEST_ASSERT_INT_WITHIN(16, whole[0].y1 << FRAC, l.y1);
}

// both endpoints far outside, the segment between crosses the window
static void test_far_both_ends(void) {
  pc = 0;
  put(CNTR); put(scal(0, 0x80)); put(stat(12));
  vctr(-4000, 0, 0); vctr(-4000, 0, 0); // move to x 290 - 3968
  vctr(4000, 0, 7); vctr(4000, 0, 7); vctr(4000, 0, 7); vctr(4000, 0, 7); put(HALT);

  avg::State s = st;
  const uint16_t n = avg::decode(mem(FRAC), s, out, 64);
  TEST_ASSERT_GREATER_OR_EQUAL(1, n);
  for (uint16_t i = 0; i < n; i++) {
    TEST_ASSERT_EQUAL(200 << FRAC, out[i].y0);
    TEST_ASSERT_EQUAL(200 << FRAC, out[i].y1);
    TEST_ASSERT_TRUE(out[i].x0 >= 0 && out[i].x1 <= 1280 << FRAC);
  }
  TEST_ASSERT_EQUAL(0, out[0].x0);
  TEST_ASSERT_EQUAL(1280 << FRAC, out[n - 1].x1);
}

// Major Havoc sparkle: a long vector splits into at most 16 segments, joined
// end to end
static void test_sparkle_cap(void) {
  for (int i = 15; i < 31; i++) {
    colorram[i] = 0x7;
  }
  pc = 0;
  put(CNTR); put(scal(0, 0)); put(0x6000 | 0x800 | (12 << 4) | 0x8);
  vctr(500, 0, 7); put(HALT); // 512 points

  avg::State s = st;
  const uint16_t n = avg::decode(mem(FRAC, avg::MHAVOC, true), s, out, 64);
  TEST_ASSERT_EQUAL(16, n);
  TEST_ASSERT_EQUAL(XC >> (16 - FRAC), out[0].x0);
  TEST_ASSERT_EQUAL(s.xpos >> (16 - FRAC), out[n - 1].x1);
  for (uint16_t i = 1; i < n; i++) {
    TEST_ASSERT_EQUAL(out[i - 1].x1, out[i].x0);
  }

  // without the split: one line
  s = st;
  TEST_ASSERT_EQUAL(1, avg::decode(mem(FRAC, avg::MHAVOC, false), s, out, 64));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_far_endpoint_keeps_slope);
  RUN_TEST(test_far_both_ends);
  RUN_TEST(test_sparkle_cap);
  return UNITY_END();
}
