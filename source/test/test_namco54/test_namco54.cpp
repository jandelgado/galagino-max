#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/cpus/mb88xx/mb88xx.cpp"
#include "../../src/emulation/namco54.cpp"
#include "../../src/machines/namco54/namco54_rom.h"

// Needs machines/namco54/namco54_rom.h (romconv galaga or xevious).
// NAMCO54_WAV=<dir> writes each rendered packet as a 24 kHz WAV.

void setUp(void) {}
void tearDown(void) {}

static const int RATE = 24000;
static const int SECONDS = 3;
static const int N = RATE * SECONDS;
static int16_t buf[N];

// galaga explosion, MAME capture (06xx control 0xa8): set A, set B, play A, play B
static const uint8_t GALAGA[] = { 0x30, 0x40, 0x00, 0x02, 0xdf, 0x40, 0x30, 0x30, 0x03, 0xdf, 0x10, 0x20 };
// xevious packets, CPU1 ROM templates (06xx control 0x68)
static const uint8_t XEVIOUS_SHIP[] = { 0x30, 0x40, 0x00, 0x02, 0xdf, 0x10, 0x10 };   // 0x15ba
static const uint8_t XEVIOUS_BIG[] = { 0x30, 0x80, 0x80, 0x01, 0xff, 0x10, 0x10 };    // 0x28b9
static const uint8_t XEVIOUS_GROUND[] = { 0x40, 0x40, 0x40, 0x01, 0xff, 0x20, 0x20 }; // 0x1a36

struct Stats {
  int peak;
  float rms;
  int last_loud; // last sample index above 1/16 of the peak
};

static void write_wav(const char *name) {
  const char *dir = getenv("NAMCO54_WAV");
  if (!dir) {
    return;
  }
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    return;
  }
  // samples are on the +-512 scale, x64 like Audio::valueToBuffer
  const uint32_t data = N * 2, riff = 36 + data, rate = RATE, bps = RATE * 2, fmt_len = 16;
  const uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f); fwrite(&bps, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  for (int i = 0; i < N; i++) {
    int v = buf[i] * 64;
    v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
    const int16_t s = (int16_t)v;
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
}

static Stats play(const uint8_t *bytes, int count, uint8_t ctrl, const char *name) {
  Namco54 *p = new Namco54(namco54_rom_data); // fresh chip per packet
  Namco54 &chip = *p;
  chip.hold(true);
  chip.render();
  chip.hold(false);
  for (int i = 0; i < RATE / 10; i++) {
    chip.render(); // let the MCU boot
  }
  for (int i = 0; i < count; i++) {
    chip.write(bytes[i], ctrl);
  }
  Stats s = { 0, 0, 0 };
  double sum = 0;
  for (int i = 0; i < N; i++) {
    buf[i] = chip.render();
    const int a = abs(buf[i]);
    if (a > s.peak) {
      s.peak = a;
    }
    sum += (double)buf[i] * buf[i];
  }
  for (int i = 0; i < N; i++) {
    if (abs(buf[i]) * 16 > s.peak) {
      s.last_loud = i;
    }
  }
  delete p;
  s.rms = (float)sqrt(sum / N);
  printf("%-15s peak %4d rms %6.1f audible %.2f s\n", name, s.peak, s.rms, s.last_loud / (float)RATE);
  write_wav(name);
  return s;
}

static void test_idle_is_silent(void) {
  Namco54 chip(namco54_rom_data);
  int peak = 0;
  for (int i = 0; i < RATE; i++) {
    const int v = abs(chip.render());
    if (i > RATE / 2 && v > peak) {
      peak = v;
    }
  }
  TEST_ASSERT_LESS_OR_EQUAL(2, peak);
}

static void test_galaga_explosion(void) {
  const Stats s = play(GALAGA, sizeof(GALAGA), 0xa8, "galaga");
  TEST_ASSERT_GREATER_THAN(50, s.peak);
  TEST_ASSERT_LESS_THAN(512, s.peak); // headroom for the WSG
  TEST_ASSERT_GREATER_THAN(RATE / 4, s.last_loud);
}

static void test_xevious_explosions(void) {
  const Stats ship = play(XEVIOUS_SHIP, sizeof(XEVIOUS_SHIP), 0x68, "xevious_ship");
  const Stats big = play(XEVIOUS_BIG, sizeof(XEVIOUS_BIG), 0x68, "xevious_big");
  const Stats ground = play(XEVIOUS_GROUND, sizeof(XEVIOUS_GROUND), 0x68, "xevious_ground");
  TEST_ASSERT_GREATER_THAN(50, ship.peak);
  TEST_ASSERT_GREATER_THAN(50, big.peak);
  TEST_ASSERT_GREATER_THAN(50, ground.peak);
}

// bytes written while the 06xx holds the chip in reset are dropped
static void test_held_chip_drops_bytes(void) {
  Namco54 chip(namco54_rom_data);
  chip.hold(true);
  for (unsigned i = 0; i < sizeof(GALAGA); i++) {
    chip.write(GALAGA[i], 0xa8);
  }
  for (int i = 0; i < RATE / 10; i++) {
    chip.render();
  }
  chip.hold(false);
  int peak = 0;
  for (int i = 0; i < RATE; i++) {
    const int v = abs(chip.render());
    if (i > RATE / 2 && v > peak) {
      peak = v;
    }
  }
  TEST_ASSERT_LESS_OR_EQUAL(2, peak);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_is_silent);
  RUN_TEST(test_galaga_explosion);
  RUN_TEST(test_xevious_explosions);
  RUN_TEST(test_held_chip_drops_bytes);
  return UNITY_END();
}
