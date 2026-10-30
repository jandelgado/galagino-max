#include <unity.h>
#include <string.h>
#include "../../src/hal/rp2350/dvi_line.h"

using namespace hal::dvi;

void setUp(void) {}
void tearDown(void) {}

static void test_swap565(void) {
  TEST_ASSERT_EQUAL_HEX16(0x3412, swap565(0x1234));
  TEST_ASSERT_EQUAL_HEX32(0x34127856, swapPair(0x12345678));
}

static void test_avg_pair_per_pixel(void) {
  TEST_ASSERT_EQUAL_HEX32(0x03E0780F, avgPair(0x07E0F800, 0x0000001F));
  TEST_ASSERT_EQUAL_HEX32(0xFFFF0000, avgPair(0xFFFF0000, 0xFFFF0000));
}

static void test_blend565(void) {
  TEST_ASSERT_EQUAL_HEX16(0xF800, blend565(0xF800, 0x001F, 0));
  TEST_ASSERT_EQUAL_HEX16(0x001F, blend565(0xF800, 0x001F, 4));
  TEST_ASSERT_EQUAL_HEX16(0xB807, blend565(0xF800, 0x001F, 1));
  TEST_ASSERT_EQUAL_HEX16(0x780F, blend565(0xF800, 0x001F, 2));
  TEST_ASSERT_EQUAL_HEX16(0x03E0, blend565(0x07E0, 0x0000, 2));
}

static void assertRow(uint32_t line, uint32_t rows, uint32_t lines, uint32_t row, uint32_t q) {
  uint32_t gotQ = 99;
  TEST_ASSERT_EQUAL_UINT32(row, scaleRow(line, rows, lines, &gotQ));
  TEST_ASSERT_EQUAL_UINT32(q, gotQ);
}

static void test_scale_row_288_on_432(void) {
  TEST_ASSERT_EQUAL_UINT32(432, scaledLines(288));
  assertRow(0, 288, 432, 0, 0);
  assertRow(1, 288, 432, 0, 2);
  assertRow(2, 288, 432, 1, 0);
  assertRow(431, 288, 432, 287, 0);
}

static void test_scale_row_240_on_480(void) {
  TEST_ASSERT_EQUAL_UINT32(480, scaledLines(240));
  assertRow(1, 240, 480, 0, 0);
  assertRow(2, 240, 480, 1, 0);
  for (uint32_t line = 0; line < 480; line++) {
    uint32_t q;
    scaleRow(line, 240, 480, &q);
    TEST_ASSERT_EQUAL_UINT32(0, q);
  }
}

static void test_scale_row_never_reads_past_last_row(void) {
  const uint32_t heights[] = {240, 288};
  for (uint32_t rows : heights) {
    const uint32_t lines = scaledLines(rows);
    for (uint32_t line = 0; line < lines; line++) {
      uint32_t q;
      const uint32_t row = scaleRow(line, rows, lines, &q);
      TEST_ASSERT_TRUE(row + (q ? 1 : 0) < rows);
    }
  }
}

// rows hold SPI byte order, as machines render
static uint16_t spi(uint16_t native) {
  return swap565(native);
}

static void test_line_1x(void) {
  const uint16_t row[4] = {spi(1), spi(2), spi(3), spi(4)};
  uint32_t out[2];
  line1x(row, out, 4, Mirror::OFF);
  TEST_ASSERT_EQUAL_HEX32(0x00020001, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00040003, out[1]);
  line1x(row, out, 4, Mirror::ON);
  TEST_ASSERT_EQUAL_HEX32(0x00030004, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00010002, out[1]);
}

static void test_line_2x(void) {
  const uint16_t a[4] = {spi(1), spi(2), spi(3), spi(4)};
  uint32_t out[4];
  line2x(a, a, 0, out, 4, Mirror::OFF);
  TEST_ASSERT_EQUAL_HEX32(0x00010001, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00020002, out[1]);
  TEST_ASSERT_EQUAL_HEX32(0x00030003, out[2]);
  TEST_ASSERT_EQUAL_HEX32(0x00040004, out[3]);
  line2x(a, a, 0, out, 4, Mirror::ON);
  TEST_ASSERT_EQUAL_HEX32(0x00040004, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00030003, out[1]);
  TEST_ASSERT_EQUAL_HEX32(0x00020002, out[2]);
  TEST_ASSERT_EQUAL_HEX32(0x00010001, out[3]);
}

// every q, both mirrors: each output pixel is blend565 of its two sources
static void test_line_2x_blends_like_blend565(void) {
  const uint16_t na[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
  const uint16_t nb[4] = {0x001F, 0x0000, 0xF81F, 0x1234};
  uint16_t a[4], b[4];
  for (uint32_t i = 0; i < 4; i++) {
    a[i] = spi(na[i]);
    b[i] = spi(nb[i]);
  }
  const Mirror mirrors[2] = {Mirror::OFF, Mirror::ON};
  for (uint32_t q = 0; q <= 4; q++) {
    for (Mirror m : mirrors) {
      uint32_t out[4];
      line2x(a, b, q, out, 4, m);
      for (uint32_t i = 0; i < 4; i++) {
        const uint32_t x = m == Mirror::ON ? 3 - i : i;
        TEST_ASSERT_EQUAL_HEX32(blend565(na[x], nb[x], q) * 0x10001u, out[i]);
      }
    }
  }
}

static void test_line_2x_224_leaves_border_black(void) {
  static uint16_t row[224];
  for (uint32_t i = 0; i < 224; i++) {
    row[i] = 0xFFFF;
  }
  // 448 of 640 pixels, centered: 96 pixels = 48 words of border each side
  static uint32_t line[320];
  memset(line, 0, sizeof(line));
  line2x(row, row, 0, line + 48, 224, Mirror::OFF);
  for (uint32_t i = 0; i < 48; i++) {
    TEST_ASSERT_EQUAL_HEX32(0, line[i]);
    TEST_ASSERT_EQUAL_HEX32(0, line[320 - 48 + i]);
  }
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, line[48]);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, line[48 + 223]);
}

// DVI strips are up to 256 wide: 512 doubled pixels
static void test_line_2x_256_fills_512_pixels(void) {
  uint16_t row[256];
  for (int x = 0; x < 256; x++) {
    row[x] = swap565((uint16_t)(x + 1));
  }
  uint32_t out[256] = {};
  line2x(row, row, 0, out, 256, Mirror::OFF);
  TEST_ASSERT_EQUAL_HEX32(0x00010001, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x01000100, out[255]);
}

static void test_line_2x_256_mirror(void) {
  uint16_t row[256];
  for (int x = 0; x < 256; x++) {
    row[x] = swap565((uint16_t)(x + 1));
  }
  uint32_t out[256] = {};
  line2x(row, row, 0, out, 256, Mirror::ON);
  TEST_ASSERT_EQUAL_HEX32(0x01000100, out[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00010001, out[255]);
}

// galagino's flipY turns the screen by 180 degrees (SPI TFT: MADCTL MY|MX),
// flipX mirrors it horizontally on top of that
static void test_orientation_follows_panel_flips(void) {
  TEST_ASSERT_TRUE(orientation(0, 0).x == Mirror::OFF && orientation(0, 0).y == Mirror::OFF);
  TEST_ASSERT_TRUE(orientation(1, 0).x == Mirror::ON && orientation(1, 0).y == Mirror::ON);
  TEST_ASSERT_TRUE(orientation(0, 1).x == Mirror::ON && orientation(0, 1).y == Mirror::OFF);
  TEST_ASSERT_TRUE(orientation(1, 1).x == Mirror::OFF && orientation(1, 1).y == Mirror::ON);
}

// strips arrive in scan order; flipY reverses the lines inside a strip
static void test_frame_row(void) {
  TEST_ASSERT_EQUAL_UINT32(0, frameRow(0, 0, Mirror::OFF));
  TEST_ASSERT_EQUAL_UINT32(7, frameRow(0, 0, Mirror::ON));
  TEST_ASSERT_EQUAL_UINT32(287, frameRow(35, 7, Mirror::OFF));
  TEST_ASSERT_EQUAL_UINT32(280, frameRow(35, 7, Mirror::ON));
  TEST_ASSERT_EQUAL_UINT32(10, frameRow(1, 2, Mirror::OFF));
}

// frames per elapsed time, not per window: a game load holds endFrame off
// for over a second while the scanout keeps 60 Hz
static void test_over_rate(void) {
  TEST_ASSERT_FALSE(overRate(60, 1000000, 75));
  TEST_ASSERT_FALSE(overRate(90, 1500000, 75));
  TEST_ASSERT_TRUE(overRate(76, 1000000, 75));
  TEST_ASSERT_TRUE(overRate(240, 2000000, 75));
}

// beam = pass * 512 + rows the pass read; 2x reads row 0 with no row done
static void test_pass_base(void) {
  TEST_ASSERT_EQUAL_UINT32(1024, passBase(1024, 512));
  TEST_ASSERT_EQUAL_UINT32(1024, passBase(1024 + 288, 512));
  TEST_ASSERT_EQUAL_UINT32(0, passBase(511, 512));
  TEST_ASSERT_EQUAL_UINT32(0xfffffe00u, passBase(0xffffffffu, 512));
}

// native mode: the strip ring lives in the 256 x 288 framebuffer
static void test_native_ring_fits_framebuffer(void) {
  TEST_ASSERT_EQUAL_UINT32(480, NATIVE_ROWS);
  TEST_ASSERT_EQUAL_UINT32(NATIVE_ROWS, NATIVE_STRIPS * NATIVE_STRIP_ROWS);
  TEST_ASSERT_TRUE(RING_SLOTS * NATIVE_STRIP_ROWS * 640 * 2 <= 256 * 288 * 2);
}

// a screen starts with the first pass whose row 0 is not built yet
static void test_native_first_strip(void) {
  TEST_ASSERT_EQUAL_UINT32(0, firstStrip(0));
  TEST_ASSERT_EQUAL_UINT32(60, firstStrip(1));
  TEST_ASSERT_EQUAL_UINT32(60, firstStrip(480));
  TEST_ASSERT_EQUAL_UINT32(120, firstStrip(481));
}

// a slot is free once the beam built the last row of the strip before it
static void test_native_slot_free(void) {
  TEST_ASSERT_TRUE(slotFree(0, 0));
  TEST_ASSERT_TRUE(slotFree(13, 0));
  TEST_ASSERT_FALSE(slotFree(14, 7));
  TEST_ASSERT_TRUE(slotFree(14, 8));
  TEST_ASSERT_FALSE(slotFree(73, 479));
  TEST_ASSERT_TRUE(slotFree(73, 480));
  // the row counter wraps after 2^32 lines: compare the difference
  TEST_ASSERT_FALSE(slotFree(0x1fffffffu, 0xffffff8fu));
  TEST_ASSERT_TRUE(slotFree(0x1fffffffu, 0xffffff90u));
  TEST_ASSERT_TRUE(slotFree(0x1fffffffu, 0x00000010u));
}

// a slot still tagged with the previous pass's strip is not shown
static void test_native_slot_tags(void) {
  const uint32_t strip = 75;
  TEST_ASSERT_EQUAL_UINT32(ringSlot(strip - RING_SLOTS), ringSlot(strip));
  TEST_ASSERT_EQUAL_UINT32(5, ringSlot(75));
  TEST_ASSERT_FALSE(stripReady(strip - RING_SLOTS, strip));
  TEST_ASSERT_FALSE(stripReady(0xffffffffu, strip));
  TEST_ASSERT_TRUE(stripReady(strip, strip));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_swap565);
  RUN_TEST(test_avg_pair_per_pixel);
  RUN_TEST(test_blend565);
  RUN_TEST(test_scale_row_288_on_432);
  RUN_TEST(test_scale_row_240_on_480);
  RUN_TEST(test_scale_row_never_reads_past_last_row);
  RUN_TEST(test_line_1x);
  RUN_TEST(test_line_2x);
  RUN_TEST(test_line_2x_blends_like_blend565);
  RUN_TEST(test_line_2x_224_leaves_border_black);
  RUN_TEST(test_line_2x_256_fills_512_pixels);
  RUN_TEST(test_line_2x_256_mirror);
  RUN_TEST(test_orientation_follows_panel_flips);
  RUN_TEST(test_frame_row);
  RUN_TEST(test_over_rate);
  RUN_TEST(test_pass_base);
  RUN_TEST(test_native_ring_fits_framebuffer);
  RUN_TEST(test_native_first_strip);
  RUN_TEST(test_native_slot_free);
  RUN_TEST(test_native_slot_tags);
  return UNITY_END();
}
