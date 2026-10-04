#include <string.h>
#include <unity.h>

#include "../../src/emulation/pokey.cpp"

// reference: MAME poly_init_9_17 states, independent of the generator
static uint32_t state17[0x1ffff];
static uint16_t state9[0x1ff];

static void build_reference() {
  uint32_t lfsr = 0x1ffff;
  for (uint32_t i = 0; i < 0x1ffff; i++) {
    uint32_t in8 = ((lfsr >> 8) ^ (lfsr >> 13)) & 1, in = lfsr & 1;
    lfsr >>= 1;
    lfsr = (lfsr & 0xff7f) | (in8 << 7);
    lfsr = (in << 16) | lfsr;
    state17[i] = lfsr;
  }
  lfsr = 0x1ff;
  for (uint32_t i = 0; i < 0x1ff; i++) {
    uint32_t in = (lfsr ^ (lfsr >> 5)) & 1;
    lfsr = (lfsr >> 1) | (in << 8);
    state9[i] = lfsr;
  }
}

void setUp(void) {}
void tearDown(void) {}

static uint32_t ref_bit(uint32_t i) { return state17[i % 0x1ffff] & 1; }

static void test_poly17_window_matches_mame(void) {
  // two full periods, 63 bits per sample, crossing the wrap
  uint32_t s = POLY17_START;
  for (uint32_t p = 0; p < 2 * 0x1ffff * 63; p += 63) {
    const uint64_t w = window17(s);
    for (uint32_t t = 0; t < 64; t++) {
      if (((w >> t) & 1) != ref_bit(p + t)) TEST_FAIL_MESSAGE("window bit mismatch");
    }
  }
}

static void test_poly17_state_at_matches_mame(void) {
  for (uint32_t i = 0; i < 0x1ffff; i++) {
    uint32_t want = 0;
    for (uint32_t k = 0; k < 17; k++) want |= ref_bit(i + k) << k;
    TEST_ASSERT_EQUAL_HEX32(want, state17_at(i));
  }
}

static void test_random_matches_mame(void) {
  for (uint32_t c = 0; c < 3 * 0x1ffff; c += 7) {
    TEST_ASSERT_EQUAL_UINT8((state17[c % 0x1ffff] >> 8) & 0xff, Pokey::random(c, false));
    TEST_ASSERT_EQUAL_UINT8(state9[c % 0x1ff] & 0xff, Pokey::random(c, true));
  }
}

static void test_random_changes_between_instructions(void) {
  // LDA $100A; EOR $100A: 4 clocks apart must not cancel out every time
  int same = 0;
  for (uint32_t c = 0; c < 1000; c++) same += Pokey::random(c, false) == Pokey::random(c + 4, false);
  TEST_ASSERT_LESS_THAN(50, same);
}

// sign changes per second of the rendered signal
static int crossings(const uint8_t *regs) {
  Pokey p;
  p.reset();
  static int16_t buf[24000];
  p.render(regs, buf, 24000);
  int n = 0;
  for (int i = 4800 + 1; i < 24000; i++) n += (buf[i - 1] < 0) != (buf[i] < 0);
  return n * 24000 / (24000 - 4801); // skip DC blocker settling
}

static void test_pure_tone_64khz_base(void) {
  uint8_t r[Pokey::REG_COUNT] = {};
  r[0] = 99; r[1] = 0xa8; r[Pokey::REG_SKCTL] = 3; // pure tone, volume 8
  // f = 1512000 / 28 / (99+1) / 2 = 270 Hz, 2 crossings per period
  TEST_ASSERT_INT_WITHIN(20, 540, crossings(r));
}

static void test_ch3_high_clock(void) {
  uint8_t r[Pokey::REG_COUNT] = {};
  r[4] = 250; r[5] = 0xa8; r[Pokey::REG_AUDCTL] = 0x20; r[Pokey::REG_SKCTL] = 3;
  // f = 1512000 / (250+4) / 2 = 2976 Hz
  TEST_ASSERT_INT_WITHIN(120, 5953, crossings(r));
}

static void test_joined_12(void) {
  uint8_t r[Pokey::REG_COUNT] = {};
  r[0] = 0xe7; r[2] = 0x03; r[3] = 0xa8; r[Pokey::REG_AUDCTL] = 0x10; r[Pokey::REG_SKCTL] = 3;
  // F = 0x3e7 = 999: f = 1512000 / 28 / 1000 / 2 = 27 Hz
  TEST_ASSERT_INT_WITHIN(4, 54, crossings(r));
}

static void test_skctl_reset_is_silent(void) {
  uint8_t r[Pokey::REG_COUNT] = {};
  r[0] = 10; r[1] = 0xaf; // SKCTL 0: held in reset
  Pokey p;
  p.reset();
  static int16_t buf[24000];
  p.render(r, buf, 24000);
  for (int i = 12000; i < 24000; i++) TEST_ASSERT_INT_WITHIN(2, 0, buf[i]);
}

static void test_short_period_bounded(void) {
  uint8_t r[Pokey::REG_COUNT] = {};
  r[4] = 0; r[5] = 0xaf; r[Pokey::REG_AUDCTL] = 0x20; r[Pokey::REG_SKCTL] = 3; // 4 clock period
  Pokey p;
  p.reset();
  int16_t buf[64];
  for (int k = 0; k < 100; k++) {
    p.render(r, buf, 64);
    for (int i = 0; i < 64; i++) TEST_ASSERT_INT_WITHIN(512, 0, buf[i]);
  }
}

static void test_long_to_short_period(void) {
  uint8_t r[Pokey::REG_COUNT] = {};
  r[0] = 255; r[1] = 0xa8; r[Pokey::REG_AUDCTL] = 0x01; r[Pokey::REG_SKCTL] = 3; // 15 kHz base, 29184 clocks
  Pokey p;
  p.reset();
  int16_t buf[64];
  p.render(r, buf, 64);
  r[0] = 9; r[Pokey::REG_AUDCTL] = 0; // now 280 clocks: 2700 Hz, ~14 sign changes per 128 samples
  int16_t tone[128];
  p.render(r, tone, 128);
  int changes = 0;
  for (int i = 1; i < 128; i++) changes += (tone[i] < 0) != (tone[i - 1] < 0);
  TEST_ASSERT_GREATER_THAN(8, changes); // a stalled channel gives 0
}

static void test_silence_when_all_zero(void) {
  uint8_t r[Pokey::REG_COUNT] = {};
  r[Pokey::REG_SKCTL] = 3;
  Pokey p;
  p.reset();
  int16_t buf[64];
  p.render(r, buf, 64);
  for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL_INT16(0, buf[i]);
}

static int32_t energy(const uint8_t *regs) {
  Pokey p;
  p.reset();
  static int16_t buf[24000];
  p.render(regs, buf, 24000);
  int64_t e = 0;
  for (int i = 4800; i < 24000; i++) e += buf[i] * buf[i];
  return (int32_t)(e / (24000 - 4800));
}

static void test_poly17_noise_is_aperiodic(void) {
  uint8_t r[Pokey::REG_COUNT] = {};
  r[0] = 0; r[1] = 0x88; r[Pokey::REG_SKCTL] = 3; // poly17 without poly5 gate, volume 8
  Pokey p;
  p.reset();
  static int16_t buf[24000];
  p.render(r, buf, 24000);
  // a period-28 tone would repeat every 4 samples (4 * 63 = 9 * 28)
  int repeats = 0;
  for (int i = 4800; i < 24000; i++) repeats += buf[i] == buf[i - 4];
  TEST_ASSERT_LESS_THAN(19200 / 4, repeats);
  TEST_ASSERT_GREATER_THAN(200, energy(r)); // fast noise partly averages out per sample
}

static void test_ch1_filter_cancels_same_frequency(void) {
  // ch3 clocks the ch1 high-pass flip-flop at the ch1 frequency: output
  // bit out ^ filter stays constant, only DC remains
  uint8_t r[Pokey::REG_COUNT] = {};
  r[0] = 99; r[1] = 0xa8; r[4] = 99; r[Pokey::REG_SKCTL] = 3;
  const int32_t open = energy(r);
  r[Pokey::REG_AUDCTL] = 0x04;
  TEST_ASSERT_LESS_THAN(open / 20, energy(r));
}

static void test_muted_channel_keeps_borrow_timing(void) {
  // ch3 poly4 muted, then audible: output must match a pokey that stepped
  // every borrow while muted (CH1_FILTER keeps ch3 live, ch1 is silent)
  uint8_t skip[Pokey::REG_COUNT] = {}, step[Pokey::REG_COUNT] = {};
  skip[4] = step[4] = 5; skip[5] = step[5] = 0xc0; skip[Pokey::REG_SKCTL] = step[Pokey::REG_SKCTL] = 3;
  step[Pokey::REG_AUDCTL] = 0x04;
  Pokey a, b;
  a.reset();
  b.reset();
  int16_t buf[64], ref[64];
  for (int k = 0; k < 30; k++) {
    a.render(skip, buf, 37);
    b.render(step, ref, 37);
  }
  skip[5] = step[5] = 0xc8;
  step[Pokey::REG_AUDCTL] = 0;
  for (int k = 0; k < 30; k++) {
    a.render(skip, buf, 64);
    b.render(step, ref, 64);
    TEST_ASSERT_EQUAL_INT16_ARRAY(ref, buf, 64);
  }
}

int main(int, char **) {
  build_reference();
  UNITY_BEGIN();
  RUN_TEST(test_poly17_window_matches_mame);
  RUN_TEST(test_poly17_state_at_matches_mame);
  RUN_TEST(test_random_matches_mame);
  RUN_TEST(test_random_changes_between_instructions);
  RUN_TEST(test_pure_tone_64khz_base);
  RUN_TEST(test_ch3_high_clock);
  RUN_TEST(test_joined_12);
  RUN_TEST(test_skctl_reset_is_silent);
  RUN_TEST(test_short_period_bounded);
  RUN_TEST(test_long_to_short_period);
  RUN_TEST(test_silence_when_all_zero);
  RUN_TEST(test_poly17_noise_is_aperiodic);
  RUN_TEST(test_ch1_filter_cancels_same_frequency);
  RUN_TEST(test_muted_channel_keeps_borrow_timing);
  return UNITY_END();
}
