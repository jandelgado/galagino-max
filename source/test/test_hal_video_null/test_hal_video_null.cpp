#include <unity.h>
#include "../../src/hal/video.h"
#include "../../src/hal/video_width.h"

void setUp(void) {}
void tearDown(void) {}

static void test_counts_submitted_strips(void) {
  hal::Video video;
  video.begin();
  video.setViewport(240);
  uint16_t strip[240 * 8] = {};
  for (int i = 0; i < 36; i++) {
    video.submit(strip, 240 * 8);
  }
  TEST_ASSERT_EQUAL_UINT32(36, video.submits());
  TEST_ASSERT_EQUAL_UINT32(36u * 1920u, video.pixels());
}

static void test_screen_parts_follow_half_rate(void) {
  hal::Video video;
  TEST_ASSERT_EQUAL_UINT8(1, video.screenParts());
  video.setHalfRate(true);
  TEST_ASSERT_EQUAL_UINT8(2, video.screenParts());
  video.setHalfRate(false);  // next machine: back to full rate
  TEST_ASSERT_EQUAL_UINT8(1, video.screenParts());
}

static void test_end_frame_returns_at_once(void) {
  hal::Video video;
  video.endFrame(0, 0);
  video.endFrame(1, 0);
  TEST_ASSERT_EQUAL_UINT32(2, video.endFrames());
}

static void test_canvas(void) {
  TEST_ASSERT_EQUAL_UINT16(240, hal::Video::CANVAS_W);
  TEST_ASSERT_EQUAL_UINT16(320, hal::Video::CANVAS_H);
}

// widest strip a machine may render: the SPI panel width
static void test_max_width(void) {
  TEST_ASSERT_EQUAL_UINT16(240, hal::VIDEO_MAX_WIDTH);
  TEST_ASSERT_EQUAL_UINT16(240, hal::Video::MAX_WIDTH);
}

static void test_strip_index_identity(void) {
  hal::Video video;
  for (uint8_t i = 0; i < 36; i++) {
    TEST_ASSERT_EQUAL_UINT8(i, video.stripIndex(i));
  }
  video.flip(1, 1);
  for (uint8_t i = 0; i < 36; i++) {
    TEST_ASSERT_EQUAL_UINT8(i, video.stripIndex(i));
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_counts_submitted_strips);
  RUN_TEST(test_screen_parts_follow_half_rate);
  RUN_TEST(test_end_frame_returns_at_once);
  RUN_TEST(test_canvas);
  RUN_TEST(test_max_width);
  RUN_TEST(test_strip_index_identity);
  return UNITY_END();
}
