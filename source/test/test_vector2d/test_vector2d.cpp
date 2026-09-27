#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "../../src/emulation/vector2d.cpp"

using namespace vec2d;

static const int W = 240, H = 288, STRIPS = H / 8, MAXL = 4096, GUARD = 64;
static uint16_t screen[W * H], ref[W * H];
static uint16_t strip_mem[GUARD + W * 8 + GUARD];
static uint16_t next_idx[MAXL], active_idx[MAXL], head_idx[STRIPS];
static Line lines[MAXL];
static uint16_t palette[256];

void setUp(void) {
  for (int i = 0; i < 256; i++) palette[i] = 0x1000 + i;
}
void tearDown(void) {}

static Line mk(int x0, int y0, int x1, int y1, uint8_t c = 1) {
  Line l = {};
  l.x0 = x0; l.y0 = y0; l.x1 = x1; l.y1 = y1; l.color = c;
  return l;
}

// reference: same Bresenham variant over the whole screen
static void ref_line(Line l) {
  if (l.y0 > l.y1) {
    int16_t t = l.x0; l.x0 = l.x1; l.x1 = t;
    t = l.y0; l.y0 = l.y1; l.y1 = t;
  }
  int x = l.x0, y = l.y0, dx = abs(l.x1 - l.x0), dy = l.y1 - l.y0;
  int sx = l.x1 < l.x0 ? -1 : 1, err = dx - dy;
  for (;;) {
    ref[y * W + x] = palette[l.color];
    if (x == l.x1 && y == l.y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x += sx; }
    if (e2 < dx) { err += dx; y++; }
  }
}

static void raster_all(Line *src, int n) {
  memcpy(lines, src, n * sizeof(Line));
  StripRasterizer r;
  r.init(next_idx, active_idx, head_idx, STRIPS);
  r.begin(lines, n);
  uint16_t *strip = strip_mem + GUARD;
  for (int s = 0; s < STRIPS; s++) {
    for (int i = 0; i < GUARD; i++) strip_mem[i] = strip_mem[GUARD + W * 8 + i] = 0xdead;
    memset(strip, 0, W * 8 * 2);
    r.render(s, strip, W, palette);
    for (int i = 0; i < GUARD; i++) {
      TEST_ASSERT_EQUAL_HEX16(0xdead, strip_mem[i]);
      TEST_ASSERT_EQUAL_HEX16(0xdead, strip_mem[GUARD + W * 8 + i]);
    }
    memcpy(screen + s * 8 * W, strip, W * 8 * 2);
  }
}

static void check(Line *src, int n) {
  memset(ref, 0, sizeof(ref));
  for (int i = 0; i < n; i++) ref_line(src[i]);
  raster_all(src, n);
  TEST_ASSERT_EQUAL_MEMORY(ref, screen, sizeof(ref));
}

static void test_matches_reference_edge_cases(void) {
  Line cases[] = {
    mk(10, 3, 200, 3),    // horizontal in one strip
    mk(0, 7, 239, 7),     // last row of strip 0
    mk(0, 8, 239, 8),     // first row of strip 1
    mk(5, 0, 5, 287),     // vertical, whole height
    mk(120, 140, 120, 140), // single point
    mk(0, 0, 239, 287),   // steep, +x
    mk(239, 0, 0, 287),   // steep, -x
    mk(0, 100, 239, 110), // shallow, +x, crosses a strip
    mk(239, 100, 0, 110), // shallow, -x
    mk(30, 7, 60, 8),     // row 7 -> row 8
    mk(30, 9, 40, 15),    // ends on last row of strip
    mk(200, 250, 100, 20), // given with y0 > y1
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) check(&cases[i], 1);
}

static void test_matches_reference_random(void) {
  static Line src[2000];
  uint32_t seed = 12345;
  for (int i = 0; i < 2000; i++) {
    int v[4];
    for (int k = 0; k < 4; k++) {
      seed = seed * 1103515245u + 12345u;
      v[k] = (seed >> 8) % (k & 1 ? H : W);
    }
    src[i] = mk(v[0], v[1], v[2], v[3], 7);
  }
  check(src, 2000);
}

static void test_empty_frame(void) {
  raster_all(lines, 0);
  for (int i = 0; i < W * H; i++) TEST_ASSERT_EQUAL_HEX16(0, screen[i]);
}

static const Rect SCR = {0, 0, W - 1, H - 1};

static void test_clip_inside_unchanged(void) {
  Line l = mk(3, 4, 200, 250);
  TEST_ASSERT_TRUE(clip(l, SCR));
  TEST_ASSERT_EQUAL(3, l.x0); TEST_ASSERT_EQUAL(4, l.y0);
  TEST_ASSERT_EQUAL(200, l.x1); TEST_ASSERT_EQUAL(250, l.y1);
}

static void test_clip_fully_outside_false(void) {
  Line a = mk(-10, 5, -1, 50), b = mk(240, 5, 400, 50);
  Line c = mk(5, -30, 50, -1), d = mk(5, 288, 50, 300);
  Line e = mk(-50, 10, 10, -50); // corner, misses
  TEST_ASSERT_FALSE(clip(a, SCR));
  TEST_ASSERT_FALSE(clip(b, SCR));
  TEST_ASSERT_FALSE(clip(c, SCR));
  TEST_ASSERT_FALSE(clip(d, SCR));
  TEST_ASSERT_FALSE(clip(e, SCR));
}

static void test_clip_crossing(void) {
  Line h = mk(-100, 50, 300, 50);
  TEST_ASSERT_TRUE(clip(h, SCR));
  TEST_ASSERT_EQUAL(0, h.x0); TEST_ASSERT_EQUAL(50, h.y0);
  TEST_ASSERT_EQUAL(239, h.x1); TEST_ASSERT_EQUAL(50, h.y1);

  Line d = mk(-10, -10, 20, 20);
  TEST_ASSERT_TRUE(clip(d, SCR));
  TEST_ASSERT_EQUAL(0, d.x0); TEST_ASSERT_EQUAL(0, d.y0);
  TEST_ASSERT_EQUAL(20, d.x1); TEST_ASSERT_EQUAL(20, d.y1);

  // crosses left and bottom edge: y = 100 + (x + 50) * 2
  Line s = mk(-50, 100, 150, 500);
  TEST_ASSERT_TRUE(clip(s, SCR));
  TEST_ASSERT_EQUAL(0, s.x0);
  TEST_ASSERT_INT_WITHIN(1, 200, s.y0);
  TEST_ASSERT_EQUAL(287, s.y1);
  TEST_ASSERT_INT_WITHIN(1, 43, s.x1); // (287 - 100) / 2 - 50 = 43.5
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_matches_reference_edge_cases);
  RUN_TEST(test_matches_reference_random);
  RUN_TEST(test_empty_frame);
  RUN_TEST(test_clip_inside_unchanged);
  RUN_TEST(test_clip_fully_outside_false);
  RUN_TEST(test_clip_crossing);
  return UNITY_END();
}
