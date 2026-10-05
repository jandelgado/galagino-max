#include <string.h>
#include <unity.h>

#include "../../src/emulation/sid.cpp"

// SID register offsets, voice 1 ($D400 + n)
static const uint8_t FREQ_LO = 0x00, FREQ_HI = 0x01, CTRL = 0x04, AD = 0x05, SR = 0x06;
static const uint8_t FC_HI = 0x16, RES_FILT = 0x17, MODE_VOL = 0x18;
static const uint8_t TRI_GATE = 0x11, SAW_GATE = 0x21, TRI_ONLY = 0x10;
static const int RATE = Sid::SAMPLE_RATE;

static Sid sid;
static int16_t buf[RATE];

void setUp(void) { sid.reset(); }
void tearDown(void) {}

static void setFreq(uint32_t hz) {
  // SID frequency register: hz * 2^24 / 985248 (PAL clock)
  const uint32_t reg = (uint32_t)(((uint64_t)hz << 24) / 985248);
  sid.write(FREQ_LO, reg & 0xff);
  sid.write(FREQ_HI, reg >> 8);
}

static int peak(const int16_t *s, int n) {
  int p = 0;
  for (int i = 0; i < n; i++) {
    const int a = s[i] < 0 ? -s[i] : s[i];
    if (a > p) {
      p = a;
    }
  }
  return p;
}

static void test_silent_after_reset(void) {
  sid.write(MODE_VOL, 0x0f);
  sid.render(buf, RATE);
  TEST_ASSERT_LESS_OR_EQUAL(2, peak(buf, RATE));
}

static void test_triangle_pitch(void) {
  setFreq(1000);
  sid.write(AD, 0x00);
  sid.write(SR, 0xf0);
  sid.write(MODE_VOL, 0x0f);
  sid.write(CTRL, TRI_GATE);
  sid.render(buf, RATE / 10); // settle attack

  sid.render(buf, RATE);
  int rising = 0;
  for (int i = 1; i < RATE; i++) {
    if (buf[i - 1] < 0 && buf[i] >= 0) {
      rising++;
    }
  }
  TEST_ASSERT_INT_WITHIN(10, 1000, rising);
}

static void test_single_voice_level(void) {
  setFreq(440);
  sid.write(SR, 0xf0);
  sid.write(MODE_VOL, 0x0f);
  sid.write(CTRL, TRI_GATE);
  sid.render(buf, RATE / 10);
  sid.render(buf, RATE / 10);
  TEST_ASSERT_INT_WITHIN(40, Sid::SAMPLE_MAX / 3, peak(buf, RATE / 10));
}

static void test_release_fades_to_floor(void) {
  setFreq(440);
  sid.write(SR, 0xf0); // release 0: about 9 ms
  sid.write(MODE_VOL, 0x0f);
  sid.write(CTRL, TRI_GATE);
  sid.render(buf, RATE / 10);
  sid.write(CTRL, TRI_ONLY); // gate off
  sid.render(buf, RATE / 10);
  sid.render(buf, RATE / 10);
  TEST_ASSERT_LESS_OR_EQUAL(4, peak(buf, RATE / 10));
}

static void test_lowpass_attenuates(void) {
  setFreq(3000); // SID tops out at 3.9 kHz (16 bit register)
  sid.write(SR, 0xf0);
  sid.write(MODE_VOL, 0x0f);
  sid.write(CTRL, SAW_GATE);
  sid.render(buf, RATE / 10);
  sid.render(buf, RATE / 10);
  const int dry = peak(buf, RATE / 10);

  sid.write(FC_HI, 0x08);
  sid.write(RES_FILT, 0x01); // voice 1 through filter, no resonance
  sid.write(MODE_VOL, 0x1f); // low pass, volume 15
  sid.render(buf, RATE / 10);
  sid.render(buf, RATE / 10);
  TEST_ASSERT_LESS_THAN(dry / 2, peak(buf, RATE / 10));
}

static void test_output_clamped(void) {
  for (uint8_t v = 0; v < 3; v++) {
    sid.write(v * 7 + FREQ_HI, 0x20);
    sid.write(v * 7 + SR, 0xf0);
    sid.write(v * 7 + CTRL, 0x41); // pulse, width 0: constant low, max |level|
  }
  sid.write(MODE_VOL, 0x0f);
  sid.render(buf, RATE / 10);
  TEST_ASSERT_LESS_OR_EQUAL(Sid::SAMPLE_MAX, peak(buf, RATE / 10));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_silent_after_reset);
  RUN_TEST(test_triangle_pitch);
  RUN_TEST(test_single_voice_level);
  RUN_TEST(test_release_fades_to_floor);
  RUN_TEST(test_lowpass_attenuates);
  RUN_TEST(test_output_clamped);
  return UNITY_END();
}
