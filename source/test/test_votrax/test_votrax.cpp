#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/votrax.cpp"
#include "../../src/emulation/speech_ring.h"
#include "../../src/machines/gorf/gorf_sc01.h"

struct Capture {
  std::vector<int16_t> s;
  void push(int16_t v) { s.push_back(v); }
};

static VotraxSc01 chip;
static uint8_t table[VotraxSc01::TABLE_BYTES] __attribute__((aligned(4)));

static const uint8_t PHONES_GORF[] = {0x1c, 0x26, 0x2b, 0x1d, 0x3f};  // G O R F STOP

static uint8_t SC01_ROM[512];

void setUp(void) {
  static bool once = false;
  if (!once) {
    Arena::init();
    Asset<unsigned char, COMPRESSED>(gorf_sc01_blob).decodeInto(SC01_ROM);
    once = true;
  }
  chip.init(SC01_ROM, table);
  chip.reset();
}
void tearDown(void) {}

// duration bits of a phone, decoded straight from the ROM (bits 37..43, inverted)
static uint8_t romDurationOf(uint8_t phone) {
  for (int i = 0; i < 64; i++) {
    uint64_t v = 0;
    for (int k = 7; k >= 0; k--) {
      v = (v << 8) | SC01_ROM[i * 8 + k];
    }
    if (((v >> 56) & 0x3f) == phone) {
      uint8_t d = 0;
      for (int b = 37; b <= 43; b++) {
        d = (uint8_t)((d << 1) | (((v >> b) & 1) ^ 1));
      }
      return d;
    }
  }
  return 0;
}

// request goes low on write, rises 72 + 16*(dur*4+1)*4*9 + 2 main clocks later
static void test_phone_timing(void) {
  TEST_ASSERT_TRUE(chip.request());
  const uint8_t phones[] = {0x03, 0x0f, 0x26};
  for (unsigned p = 0; p < sizeof(phones); p++) {
    chip.write(phones[p]);
    TEST_ASSERT_FALSE(chip.request());

    const uint32_t dur = romDurationOf(phones[p]);
    const double clocks = 72 + 16.0 * (dur * 4 + 1) * 4 * 9 + 2;
    const double expect = clocks * VotraxSc01::LINE_HZ / 756000.0;

    Capture cap;
    int lines = 0;
    while (!chip.request() && lines < 100000) {
      chip.line(cap);
      lines++;
    }
    TEST_ASSERT_TRUE(fabs(lines - expect) <= 1.0);
  }
}

// f1 index 5 from MAME's build_standard_filter, evaluated here in double
static void test_f1_coefficients(void) {
  const double cclock = 756000 / 36.0, sclock = 756000 / 18.0;
  const double c1t = 11247, c1b = 11797, c2t = 949, c2b = 52067, c4 = 166272;
  const double c3 = 2280 + 2546 + 9861;  // bits 0 and 2 of 5
  const double k0 = c1t / (cclock * c1b);
  const double k1 = c4 * c2t / (cclock * c1b * c3);
  const double k2 = c4 * c2b / (cclock * cclock * c1b * c3);
  const double fpeak = sqrt(fabs(k0 * k1 - k2)) / (2 * M_PI * k2);
  const double zc = 2 * M_PI * fpeak / tan(M_PI * fpeak / sclock);
  const double m0 = zc * k0, m1 = zc * k1, m2 = zc * zc * k2;
  const double a[4] = {1 + m0, 3 + m0, 3 - m0, 1 - m0};
  const double b[4] = {1 + m1 + m2, 3 + m1 - m2, 3 - m1 - m2, 1 - m1 + m2};

  const VotraxCoef &c = chip.f1Coef(5);
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_TRUE(fabs(a[i] / b[0] - c.a[i]) < 1e-5);
  }
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(fabs(b[i + 1] / b[0] - c.b[i]) < 1e-5);
  }
}

// every F2/F2Q (and F1, F3) entry is finite and its impulse response decays
static void test_filters_stable(void) {
  const VotraxCoef *sets[3] = {&chip.f1Coef(0), &chip.f2Coef(0), &chip.f3Coef(0)};
  const int counts[3] = {16, 512, 16};
  for (int t = 0; t < 3; t++) {
    for (int e = 0; e < counts[t]; e++) {
      const VotraxCoef &c = sets[t][e];
      double x[4] = {0, 0, 0, 0}, y[3] = {0, 0, 0};
      double peak = 0, tail = 0;
      for (int n = 0; n < 6000; n++) {
        x[3] = x[2];
        x[2] = x[1];
        x[1] = x[0];
        x[0] = n == 0 ? 1.0 : 0.0;
        const double o = c.a[0] * x[0] + c.a[1] * x[1] + c.a[2] * x[2] + c.a[3] * x[3] -
                         c.b[0] * y[0] - c.b[1] * y[1] - c.b[2] * y[2];
        y[2] = y[1];
        y[1] = y[0];
        y[0] = o;
        TEST_ASSERT_TRUE(isfinite(o));
        if (fabs(o) > peak) {
          peak = fabs(o);
        }
        if (n >= 5900 && fabs(o) > tail) {
          tail = fabs(o);
        }
      }
      TEST_ASSERT_TRUE(peak < 100.0);
      TEST_ASSERT_TRUE(tail < 1e-2 * (peak > 1 ? peak : 1));
    }
  }
}

static void speak(const uint8_t *phones, int n, Capture &cap) {
  for (int i = 0; i < n; i++) {
    chip.write(phones[i]);
    int guard = 0;
    while (!chip.request() && guard++ < 100000) {
      chip.line(cap);
    }
  }
}

static void dumpWav(const char *name, const std::vector<int16_t> &s, uint32_t hz) {
  const char *dir = getenv("TMPDIR");
  if (!dir) {
    return;
  }
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    return;
  }
  const uint32_t bytes = (uint32_t)s.size() * 2, rate = hz, br = hz * 2;
  const uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
  const uint32_t riff = 36 + bytes, fmtLen = 16;
  fwrite("RIFF", 1, 4, f);
  fwrite(&riff, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmtLen, 4, 1, f);
  fwrite(&fmt, 2, 1, f);
  fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f);
  fwrite(&br, 4, 1, f);
  fwrite(&align, 2, 1, f);
  fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f);
  fwrite(&bytes, 4, 1, f);
  fwrite(s.data(), 2, s.size(), f);
  fclose(f);
  printf("wrote %s (%u samples)\n", path, (unsigned)s.size());
}

// G O R F: audible, bounded, finite; analog work stops after STOP
static void test_phrase_bounded(void) {
  Capture cap;
  speak(PHONES_GORF, sizeof(PHONES_GORF), cap);

  int peak = 0;
  for (size_t i = 0; i < cap.s.size(); i++) {
    const int v = abs((int)cap.s[i]);
    peak = v > peak ? v : peak;
  }
  printf("phrase: %u samples, peak %d, busy after STOP: %d\n", (unsigned)cap.s.size(), peak,
         (int)chip.busy());
  TEST_ASSERT_TRUE(peak > 500);
  TEST_ASSERT_TRUE(peak < 32767);

  // let the tail ring out
  size_t before = cap.s.size();
  int lines = 0;
  while (chip.busy() && lines < 100000) {
    chip.line(cap);
    lines++;
  }
  printf("tail: %d lines, %u samples\n", lines, (unsigned)(cap.s.size() - before));
  TEST_ASSERT_FALSE(chip.busy());
  const size_t quietAt = cap.s.size();
  for (int i = 0; i < 100; i++) {
    chip.line(cap);
  }
  TEST_ASSERT_EQUAL_UINT32(quietAt, cap.s.size());
  TEST_ASSERT_TRUE(chip.request());

  dumpWav("votrax_gorf.wav", cap.s, chip.sampleHz());
}

// the same through the real ring type, 782 kHz clock select
static void test_ring_and_clock_select(void) {
  static SpeechRingT<2048, 768> ring;
  ring.reset(chip.sampleHz(), 24000);
  uint8_t phones[sizeof(PHONES_GORF)];
  for (unsigned i = 0; i < sizeof(phones); i++) {
    phones[i] = PHONES_GORF[i] | 0x40;
  }
  for (int i = 0; i < 2; i++) {
    chip.write(phones[i]);
    int guard = 0;
    while (!chip.request() && guard++ < 100000) {
      chip.line(ring);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(43444, chip.sampleHz());
  TEST_ASSERT_TRUE(ring.count() > 100);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_phone_timing);
  RUN_TEST(test_f1_coefficients);
  RUN_TEST(test_filters_stable);
  RUN_TEST(test_phrase_bounded);
  RUN_TEST(test_ring_and_clock_select);
  return UNITY_END();
}
