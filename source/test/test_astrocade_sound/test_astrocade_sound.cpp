#include <unity.h>
#include <string.h>
#include <time.h>

#include "../../src/emulation/astrocade_sound.cpp"

static AstrocadeSound chip;
static int32_t buf[24000];

void setUp(void) {
  chip.reset();
  memset(buf, 0, sizeof(buf));
}
void tearDown(void) {}

// Tone A period: 2 * (reg0 + 1) * (reg1 + 1) chip clocks. reg0 = 15,
// reg1 = 63 -> 2048 clocks = 873.9 Hz. Counted over one second of output.
static void test_tone_frequency(void) {
  chip.write(0, 15);
  chip.write(1, 63);
  chip.write(6, 0x0f);
  chip.render(buf, 24000, 1);

  int rising = 0;
  for (int i = 1; i < 24000; i++) {
    if (buf[i - 1] == 0 && buf[i] > 0) {
      rising++;
    }
  }
  const double expect = 1789773.0 / 2048.0;
  TEST_ASSERT_TRUE(rising > expect * 0.99 && rising < expect * 1.01);
}

static void test_zero_registers_silent(void) {
  chip.render(buf, 24000, 1);
  for (int i = 0; i < 24000; i++) {
    TEST_ASSERT_EQUAL_INT32(0, buf[i]);
  }
}

// Reference: MAME sound_stream_update stepped per master/noise boundary
// (the first port, 75 passes per sample at reset registers)
struct RefChip {
  uint8_t regs[8] = {};
  uint8_t master = 0, noiseClk = 0, aCount = 0, bCount = 0, cCount = 0;
  bool a = false, b = false, c = false;
  uint16_t noise = 0, vibrato = 0;
  uint32_t frac = 0;

  static uint8_t reverse8(uint8_t x) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
      r = (uint8_t)((r << 1) | ((x >> i) & 1));
    }
    return r;
  }

  void render(int32_t *acc, int n, int gain) {
    for (int i = 0; i < n; i++) {
      frac += 4887244;
      int32_t left = (int32_t)(frac >> 16);
      frac &= 0xffff;
      const int32_t clocks = left;
      int32_t sum = 0;
      while (left > 0) {
        int32_t chunk = 256 - master;
        if (chunk > left) {
          chunk = left;
        }
        if (chunk > 64 - noiseClk) {
          chunk = 64 - noiseClk;
        }
        int32_t level = (a ? (regs[6] & 0x0f) : 0) + (b ? (regs[6] >> 4) : 0) + (c ? (regs[5] & 0x0f) : 0);
        if ((regs[5] & 0x20) && (noise & 0x4000)) {
          level += regs[7] >> 4;
        }
        sum += level * chunk;
        left -= chunk;
        noiseClk = (uint8_t)(noiseClk + chunk);
        if (noiseClk >= 64) {
          noise = AstrocadeSound::noiseNext(noise);
          noiseClk = (uint8_t)(noiseClk - 64);
          vibrato++;
        }
        master = (uint8_t)(master + chunk);
        if (master != 0) {
          continue;
        }
        master = (uint8_t)~regs[0];
        if ((regs[5] & 0x10) == 0) {
          if (!((vibrato >> (regs[4] >> 6)) & 0x0200)) {
            master = (uint8_t)(master + (regs[4] & 0x3f));
          }
        } else {
          master = (uint8_t)(master + (reverse8((noise >> 7) & 0xff) & regs[7]));
        }
        if (++aCount == 0) { a = !a; aCount = (uint8_t)~regs[1]; }
        if (++bCount == 0) { b = !b; bCount = (uint8_t)~regs[2]; }
        if (++cCount == 0) { c = !c; cCount = (uint8_t)~regs[3]; }
      }
      acc[i] += sum * gain / clocks;
    }
  }
};

// register sets: reset (master period 1 clock), plain tones, vibrato,
// noise-modulated master, noise AM; registers change between chunks
static const uint8_t SCENES[][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0x00},
    {0x0f, 0x3f, 0x50, 0x07, 0x00, 0x0c, 0x9f, 0x00},
    {0x03, 0x10, 0x22, 0xf0, 0xd5, 0x0a, 0x5a, 0x00},
    {0x01, 0x80, 0x02, 0x41, 0x00, 0x3f, 0x77, 0xf3},
    {0xfe, 0x01, 0xff, 0x00, 0x7f, 0x2f, 0xff, 0xff},
    {0x80, 0xc0, 0x09, 0x33, 0x2a, 0x1c, 0x18, 0x5c},
};

static void test_matches_per_clock_reference(void) {
  static RefChip ref;
  static int32_t want[64];
  int32_t got[64];
  for (unsigned s = 0; s < sizeof(SCENES) / sizeof(SCENES[0]); s++) {
    for (int rep = 0; rep < 300; rep++) {
      for (int r = 0; r < 8; r++) {
        chip.write(r, SCENES[s][r]);
        ref.regs[r] = SCENES[s][r];
      }
      memset(got, 0, sizeof(got));
      memset(want, 0, sizeof(want));
      chip.render(got, 64, 4);
      ref.render(want, 64, 4);
      TEST_ASSERT_EQUAL_INT32_ARRAY(want, got, 64);
    }
  }
}

// Audio::transmit() renders until I2S is full: rendering must stay far
// below real time (64 samples = 2.67 ms) even at reset registers, where
// the master counter reloads every chip clock
static void test_reset_registers_cost(void) {
  static RefChip ref;
  const int CHUNKS = 2000;
  int32_t out[64];

  const clock_t r0 = clock();
  for (int i = 0; i < CHUNKS; i++) {
    ref.render(out, 64, 4);
  }
  const clock_t r1 = clock();
  for (int i = 0; i < CHUNKS; i++) {
    chip.render(out, 64, 4);
  }
  const clock_t r2 = clock();

  // reference: 75 passes per sample, spans: 2-3 (measured 5x at -O0)
  TEST_ASSERT_TRUE((r2 - r1) * 3 < (r1 - r0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_tone_frequency);
  RUN_TEST(test_zero_registers_silent);
  RUN_TEST(test_matches_per_clock_reference);
  RUN_TEST(test_reset_registers_cost);
  return UNITY_END();
}
