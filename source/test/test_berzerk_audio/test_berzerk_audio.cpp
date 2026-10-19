#include <unity.h>
#include <string.h>

#include "../../src/emulation/s14001a.cpp"
#include "../../src/emulation/exidy6840.cpp"
#include "../../src/machines/berzerk/berzerk_audio.cpp"

enum : uint8_t { SPEECH = 4, SFXCTRL = 6, BUSY_CLEAR = 0x40 };

static uint8_t rom[4096];
static BerzerkAudio audio;

// word 0: unvoiced, add-3 deltas, length 0x10 quarter periods
void setUp(void) {
  memset(rom, 0, sizeof(rom));
  rom[0] = 0x01;
  rom[0x10] = 0x08;
  rom[0x11] = 0x80 | 0x18;
  for (int i = 0x80; i < 0x400; i++) {
    rom[i] = 0xff;
  }
  audio.reset(rom);
}
void tearDown(void) {}

// busy rises on the chip's first clock after the word write (as MAME)
static int lines_until_idle(int limit) {
  int lines = 0;
  while (audio.read(SPEECH) == BUSY_CLEAR && lines < limit) {
    audio.line();
    lines++;
  }
  while (audio.read(SPEECH) != BUSY_CLEAR && lines < limit) {
    audio.line();
    lines++;
  }
  return lines;
}

static void test_busy_after_start(void) {
  audio.write(SPEECH, 0x40 | (7 << 3) | 7); // full gain, fastest clock
  audio.write(SPEECH, 0x00);                // word 0
  TEST_ASSERT_EQUAL_UINT8(BUSY_CLEAR, audio.read(SPEECH));
  audio.line();
  TEST_ASSERT_EQUAL_UINT8(0x00, audio.read(SPEECH));
  const int lines = lines_until_idle(200000);
  TEST_ASSERT_TRUE(lines > 0 && lines < 200000);
}

// fastest clock (34.7 kHz) finishes a word in fewer lines than slowest (19.5 kHz)
static void test_clock_change_while_speaking(void) {
  audio.write(SPEECH, 0x40 | (7 << 3) | 0);
  audio.write(SPEECH, 0x00);
  const int slow = lines_until_idle(200000);

  audio.write(SPEECH, 0x00);
  for (int i = 0; i < 10; i++) {
    audio.line();
  }
  audio.write(SPEECH, 0x40 | (7 << 3) | 7); // mid-word
  const int fast = lines_until_idle(200000) + 10;
  TEST_ASSERT_TRUE(fast < slow);
}

static void drain(int16_t *out, int calls, int *nonzero) {
  for (int k = 0; k < calls; k++) {
    audio.render(out, 64);
    for (int i = 0; i < 64; i++) {
      *nonzero += out[i] != 0;
    }
  }
}

static void test_gain_zero_is_silent(void) {
  audio.write(SPEECH, 0x40); // gain 0
  audio.write(SPEECH, 0x00);
  for (int i = 0; i < 2000; i++) {
    audio.line();
  }
  int16_t out[64];
  int nonzero = 0;
  drain(out, 40, &nonzero);
  TEST_ASSERT_EQUAL_INT(0, nonzero);
}

static void test_speech_reaches_ring(void) {
  audio.write(SPEECH, 0x40 | (7 << 3) | 7);
  audio.write(SPEECH, 0x00);
  for (int i = 0; i < 2000; i++) {
    audio.line();
  }
  int16_t out[64];
  int nonzero = 0;
  drain(out, 40, &nonzero);
  TEST_ASSERT_TRUE(nonzero > 0);
}

static void test_sfx_reaches_ring(void) {
  audio.write(1, 0x82); // ch1: 16 bit, internal clock, output on
  audio.write(2, 0x01); // MSB latch, shared by 6840 offsets 2/4/6
  audio.write(5, 0xbe);                 // ~1 kHz
  audio.write(SFXCTRL, (2 << 6) | 7);   // ch1 volume 7
  for (int i = 0; i < 2000; i++) {
    audio.line();
  }
  int16_t out[64];
  int nonzero = 0;
  drain(out, 40, &nonzero);
  TEST_ASSERT_TRUE(nonzero > 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_busy_after_start);
  RUN_TEST(test_clock_change_while_speaking);
  RUN_TEST(test_gain_zero_is_silent);
  RUN_TEST(test_speech_reaches_ring);
  RUN_TEST(test_sfx_reaches_ring);
  return UNITY_END();
}
