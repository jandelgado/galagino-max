#include <unity.h>
#include "../../src/hal/rp2350/strip_ring.h"

using namespace hal::dvi;

typedef StripRing<8> Ring;

void setUp(void) {}
void tearDown(void) {}

static void fill(uint16_t *slot, uint16_t value) {
  for (uint32_t i = 0; i < Ring::STRIP_PIXELS; i++) {
    slot[i] = value;
  }
}

static void publishStrips(Ring &ring, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    uint16_t *slot = ring.tryAcquire();
    TEST_ASSERT_NOT_NULL(slot);
    ring.publish();
  }
}

static void test_no_frame_started_shows_black(void) {
  static Ring ring;
  ring.startScan();
  TEST_ASSERT_NULL(ring.row(0, Mirror::OFF));
  TEST_ASSERT_EQUAL_UINT32(0, ring.lateLines());
}

static void test_published_rows_visible(void) {
  static Ring ring;
  ring.startScan();
  ring.startWrite();
  fill(ring.tryAcquire(), 7);
  ring.publish();
  for (uint16_t r = 0; r < 8; r++) {
    const uint16_t *row = ring.row(r, Mirror::OFF);
    TEST_ASSERT_NOT_NULL(row);
    TEST_ASSERT_EQUAL_UINT16(7, row[0]);
  }
  TEST_ASSERT_NULL(ring.row(8, Mirror::OFF));
  TEST_ASSERT_EQUAL_UINT32(1, ring.lateLines());
}

static void test_ring_full_blocks_until_release(void) {
  static Ring ring;
  ring.startScan();
  ring.startWrite();
  uint16_t *first = ring.tryAcquire();
  publishStrips(ring, 8);
  TEST_ASSERT_NULL(ring.tryAcquire());
  ring.release(8);
  TEST_ASSERT_EQUAL_PTR(first, ring.tryAcquire());
}

static void test_writer_behind_never_blocks(void) {
  static Ring ring;
  ring.startScan();
  ring.startWrite();
  publishStrips(ring, 8);
  ring.startScan();  // the writer missed this frame
  publishStrips(ring, 36);
  TEST_ASSERT_NULL(ring.row(0, Mirror::OFF));
  TEST_ASSERT_EQUAL_UINT32(0, ring.lateLines());
}

static void test_flip_y_row_mapping(void) {
  static Ring ring;
  ring.startScan();
  ring.startWrite();
  uint16_t *slot = ring.tryAcquire();
  for (uint16_t line = 0; line < 8; line++) {
    slot[line * Ring::WIDTH] = line;
  }
  ring.publish();
  TEST_ASSERT_EQUAL_UINT16(7, ring.row(0, Mirror::ON)[0]);
  TEST_ASSERT_EQUAL_UINT16(0, ring.row(7, Mirror::ON)[0]);
  TEST_ASSERT_EQUAL_UINT16(0, ring.row(0, Mirror::OFF)[0]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_no_frame_started_shows_black);
  RUN_TEST(test_published_rows_visible);
  RUN_TEST(test_ring_full_blocks_until_release);
  RUN_TEST(test_writer_behind_never_blocks);
  RUN_TEST(test_flip_y_row_mapping);
  return UNITY_END();
}
