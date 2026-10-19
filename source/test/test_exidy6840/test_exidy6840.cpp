#include <unity.h>

#include "../../src/emulation/exidy6840.cpp"

static Exidy6840 ptm;

void setUp(void) { ptm.reset(24000); }
void tearDown(void) {}

// channel 1, 16 bit, internal clock, output on: CR = 0x82
static void setup_ch1(uint16_t latch) {
  ptm.ptm_w(1, 0x82);
  ptm.ptm_w(4, latch >> 8);
  ptm.ptm_w(5, latch & 0xff);
  ptm.sfxctrl_w(2, 7); // ch1 full volume
}

static void test_silent_after_reset(void) {
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL_INT32(0, ptm.sample());
  }
}

// half period = latch + 1 clocks: 894886 / (2 * 447) = ~1001 Hz
static void test_square_frequency(void) {
  setup_ch1(446);
  int edges = 0, prev = 0;
  for (int i = 0; i < 24000; i++) {
    const int s = ptm.sample() > Exidy6840::BASE_VOLUME / 2;
    edges += s && !prev;
    prev = s;
  }
  TEST_ASSERT_INT_WITHIN(3, 1001, edges);
}

static void test_volume_scales(void) {
  setup_ch1(446);
  ptm.sfxctrl_w(2, 3);
  int32_t peak = 0;
  for (int i = 0; i < 200; i++) {
    const int32_t s = ptm.sample();
    peak = s > peak ? s : peak;
  }
  TEST_ASSERT_EQUAL_INT32(3 * Exidy6840::BASE_VOLUME / 7, peak);
}

// CR bit 7 clear: no output
static void test_output_disable(void) {
  setup_ch1(446);
  ptm.ptm_w(1, 0x02);
  for (int i = 0; i < 200; i++) {
    TEST_ASSERT_EQUAL_INT32(0, ptm.sample());
  }
}

// timer 0 clocked by noise (CR bit 1 clear): neither silent nor always on
static void test_noise_clocked_channel(void) {
  ptm.ptm_w(1, 0x01); // ch1 CR bit 0 set: offset 0 writes ch0
  ptm.ptm_w(0, 0x80); // ch0: external clock, output on, bit 0 clear runs all
  ptm.ptm_w(1, 0x00);
  ptm.ptm_w(2, 0x00);
  ptm.ptm_w(3, 0x02);
  ptm.sfxctrl_w(1, 7);
  int high = 0;
  for (int i = 0; i < 24000; i++) {
    high += ptm.sample() > 0;
  }
  TEST_ASSERT_TRUE(high > 100 && high < 23900);
}

static void test_counter_read_latches_lsb(void) {
  setup_ch1(0x1234);
  TEST_ASSERT_EQUAL_UINT8(0x12, ptm.ptm_r(4));
  TEST_ASSERT_EQUAL_UINT8(0x34, ptm.ptm_r(5));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_silent_after_reset);
  RUN_TEST(test_square_frequency);
  RUN_TEST(test_volume_scales);
  RUN_TEST(test_output_disable);
  RUN_TEST(test_noise_clocked_channel);
  RUN_TEST(test_counter_read_latches_lsb);
  return UNITY_END();
}
