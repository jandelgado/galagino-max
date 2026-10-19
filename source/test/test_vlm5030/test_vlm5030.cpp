#include <string.h>
#include <unity.h>

#include "../../src/emulation/vlm5030.cpp"

// Expected sample numbers follow MAME vlm5030.cpp sound_stream_update by
// hand. Default parameters: 40 samples per interpolation step, 4 steps per
// frame. After ST low the chip counts one frame (4 steps) before parsing,
// so frames parse at samples 200, 360, 520, ...

static const int SHIFT = 6;   // 10 bit filter output to int16
static const int FRAME = 160; // samples per frame, normal speed
static const int FIRST_PARSE = 200;

static uint8_t rom[512];
static Vlm5030 vlm;
static int16_t out[1024];

void setUp(void) {
  memset(rom, 0, sizeof(rom));
  vlm.reset(rom, sizeof(rom));
}
void tearDown(void) {}

// LSB first bit fields (MAME get_bits)
static void put(uint8_t *at, int bit, int bits, uint32_t v) {
  for (int i = 0; i < bits; i++, bit++) {
    at[bit >> 3] |= ((v >> i) & 1) << (bit & 7);
  }
}

// speech frame: energy, pitch and K indexes, K1 index 48 is 0
static void frame(uint8_t *at, uint8_t energy, uint8_t pitch, uint8_t k3 = 0) {
  memset(at, 0, 6);
  put(at, 1, 5, pitch);
  put(at, 6, 5, energy);
  put(at, 33, 4, k3);
  put(at, 42, 6, 48);
}

static void phrase(int number, uint16_t address) {
  const int table = (number & 0xfe) + ((number & 1) << 8);
  rom[table] = address >> 8;
  rom[table + 1] = address & 0xff;
}

static void speak(uint8_t number) {
  vlm.data_w(number);
  vlm.st_w(true);
  vlm.st_w(false);
}

// samples until BSY drops, at most max
static int run(int max) {
  int n = 0;
  while (n < max && vlm.bsy()) {
    out[n++] = vlm.sample();
  }
  return n;
}

static void test_idle(void) {
  TEST_ASSERT_FALSE(vlm.bsy());
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL_INT16(0, vlm.sample());
  }
}

static void test_st_high_busy(void) {
  vlm.data_w(0);
  vlm.st_w(true);
  TEST_ASSERT_TRUE(vlm.bsy());
}

// silent command 0x05: 4 frames; end parses at 200 + 4 * 160, BSY drops
// one step later
static void test_silent_frames(void) {
  phrase(0, 0x10);
  rom[0x10] = 0x05;
  rom[0x11] = 0x03;
  speak(0);
  TEST_ASSERT_EQUAL_INT(FIRST_PARSE + 4 * FRAME + 40 + 1, run(1000));
}

// odd phrase numbers read the table at 0x100
static void test_odd_phrase_table(void) {
  phrase(1, 0x20);
  rom[0x20] = 0x05;
  rom[0x21] = 0x03;
  speak(1);
  TEST_ASSERT_EQUAL_INT(FIRST_PARSE + 4 * FRAME + 40 + 1, run(1000));
}

// K all 0: the lattice passes the excitation. Frame 1 parses silent (old
// energy 0), pulses of energy 127 every 21 samples from frame 2 on; the
// end frame interpolates energy to 96 and pitch to 16 for one step.
static void test_voiced_pulses(void) {
  phrase(0, 0x10);
  frame(rom + 0x10, 31, 1);
  frame(rom + 0x16, 31, 1);
  frame(rom + 0x1c, 31, 1);
  rom[0x22] = 0x03;
  speak(0);
  const int n = run(1000);
  TEST_ASSERT_EQUAL_INT(FIRST_PARSE + 3 * FRAME + 40 + 1, n);

  const int voiced = FIRST_PARSE + FRAME, end = FIRST_PARSE + 3 * FRAME;
  for (int i = 0; i < n; i++) {
    int expect = 0;
    if (i >= voiced && i < end && (i - voiced) % 21 == 0) {
      expect = 127;
    }
    if (i == end + 11 || i == end + 27) {
      expect = 96;
    }
    TEST_ASSERT_EQUAL_INT16_MESSAGE(expect << SHIFT, out[i], "sample");
  }
}

// K3 = 256 (index 4): the pulse returns as -(-256 * 127 / 512) = 63
// three samples later, C truncation toward zero
static void test_lattice_reflection(void) {
  phrase(0, 0x10);
  frame(rom + 0x10, 31, 30, 4);
  frame(rom + 0x16, 31, 30, 4);
  rom[0x1c] = 0x03;
  speak(0);
  run(1000);
  const int voiced = FIRST_PARSE + FRAME;
  TEST_ASSERT_EQUAL_INT16(127 << SHIFT, out[voiced]);
  TEST_ASSERT_EQUAL_INT16(0, out[voiced + 1]);
  TEST_ASSERT_EQUAL_INT16(0, out[voiced + 2]);
  TEST_ASSERT_EQUAL_INT16(63 << SHIFT, out[voiced + 3]);
}

// RST high to low latches the parameter byte: speed 1 is 30 samples per
// step
static void test_speed_parameter(void) {
  phrase(0, 0x10);
  rom[0x10] = 0x05;
  rom[0x11] = 0x03;
  vlm.data_w(0x08);
  vlm.rst_w(true);
  vlm.rst_w(false);
  speak(0);
  TEST_ASSERT_EQUAL_INT(5 * 30 + 16 * 30 + 30 + 1, run(1000));
}

// RST rising while busy resets the chip
static void test_rst_while_busy(void) {
  phrase(0, 0x10);
  rom[0x10] = 0x05;
  rom[0x11] = 0x03;
  speak(0);
  run(100);
  vlm.rst_w(true);
  TEST_ASSERT_FALSE(vlm.bsy());
  TEST_ASSERT_EQUAL_INT16(0, vlm.sample());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_idle);
  RUN_TEST(test_st_high_busy);
  RUN_TEST(test_silent_frames);
  RUN_TEST(test_odd_phrase_table);
  RUN_TEST(test_voiced_pulses);
  RUN_TEST(test_lattice_reflection);
  RUN_TEST(test_speed_parameter);
  RUN_TEST(test_rst_while_busy);
  return UNITY_END();
}
