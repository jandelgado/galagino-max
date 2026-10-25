#include <unity.h>
#include "../../src/hal/hal.h"
#include "../../src/hal/led_color.h"

void setUp(void) {}
void tearDown(void) {}

static void test_size_and_packed_constructor(void) {
  TEST_ASSERT_EQUAL_UINT32(3, sizeof(CRGB));
  const CRGB c(0x102030);
  TEST_ASSERT_EQUAL_UINT8(0x10, c.raw[0]);
  TEST_ASSERT_EQUAL_UINT8(0x20, c.raw[1]);
  TEST_ASSERT_EQUAL_UINT8(0x30, c.raw[2]);
  TEST_ASSERT_EQUAL_UINT8(0x10, c.r);
  TEST_ASSERT_EQUAL_UINT8(0x20, c.g);
  TEST_ASSERT_EQUAL_UINT8(0x30, c.b);
}

// FastLED values (note CRGB::Green is HTML green, 0x008000)
static void test_named_colors_match_fastled(void) {
  TEST_ASSERT_EQUAL_HEX32(0x000000, CRGB::Black);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, CRGB::Red);
  TEST_ASSERT_EQUAL_HEX32(0x008000, CRGB::Green);
  TEST_ASSERT_EQUAL_HEX32(0x0000FF, CRGB::Blue);
  TEST_ASSERT_EQUAL_HEX32(0xFFFF00, CRGB::Yellow);
  TEST_ASSERT_EQUAL_HEX32(0xFF00FF, CRGB::Magenta);
  TEST_ASSERT_EQUAL_HEX32(0x00FFFF, CRGB::Cyan);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, CRGB::White);
  const CRGB c = CRGB::Red;
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.r);
}

// FastLED scale8 (FASTLED_SCALE8_FIXED): (v * (1 + scale)) >> 8
static void test_nscale8(void) {
  CRGB c(255, 128, 0);
  c.nscale8(128);
  TEST_ASSERT_EQUAL_UINT8(128, c.r);
  TEST_ASSERT_EQUAL_UINT8(64, c.g);
  TEST_ASSERT_EQUAL_UINT8(0, c.b);
  CRGB full(255, 1, 7);
  full.nscale8(255);
  TEST_ASSERT_EQUAL_UINT8(255, full.r);
  TEST_ASSERT_EQUAL_UINT8(1, full.g);
  TEST_ASSERT_EQUAL_UINT8(7, full.b);
  TEST_ASSERT_EQUAL_UINT8(0, CRGB(200, 0, 0).nscale8(0).r);
}

static void test_fill_solid(void) {
  CRGB leds[4];
  fill_solid(leds, 4, CRGB(1, 2, 3));
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_UINT8(3, leds[i].b);
  }
}

// FastLED blend8 (FASTLED_BLEND_FIXED): (a*256 + b + (b - a) * amount) >> 8
static void test_nblend(void) {
  CRGB a(10, 0, 255);
  nblend(a, CRGB(200, 255, 0), 0);
  TEST_ASSERT_EQUAL_UINT8(10, a.r);
  nblend(a, CRGB(200, 255, 0), 255);
  TEST_ASSERT_EQUAL_UINT8(200, a.r);
  TEST_ASSERT_EQUAL_UINT8(255, a.g);
  TEST_ASSERT_EQUAL_UINT8(0, a.b);
  CRGB up(0, 255, 0);
  nblend(up, CRGB(255, 0, 0), 128);
  TEST_ASSERT_EQUAL_UINT8(128, up.r);
  TEST_ASSERT_EQUAL_UINT8(127, up.g);
}

static void test_host_strip_keeps_last_frame(void) {
  hal::LedStrip::init(16, 2);
  const uint8_t rgb[6] = {1, 2, 3, 4, 5, 6};
  hal::LedStrip::show(rgb, 2);
  TEST_ASSERT_EQUAL_UINT32(2, hal::host::ledCount());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rgb, hal::host::ledFrame(), 6);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_size_and_packed_constructor);
  RUN_TEST(test_named_colors_match_fastled);
  RUN_TEST(test_nscale8);
  RUN_TEST(test_fill_solid);
  RUN_TEST(test_nblend);
  RUN_TEST(test_host_strip_keeps_last_frame);
  return UNITY_END();
}
