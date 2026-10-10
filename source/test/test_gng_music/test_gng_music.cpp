#include <unity.h>

#include "../../src/machines/gng/gng_music.h"

void setUp(void) {}
void tearDown(void) {}

static void assertCue(uint8_t command, GngMusic action, uint8_t song) {
  const GngCue cue = gngMusicCue(command);
  TEST_ASSERT_EQUAL(action, cue.action);
  TEST_ASSERT_EQUAL(song, cue.song);
}

// MAME trace 2026-10-10: 35 credit, 36 game start, 2b stage 1 loop
static void test_loops_play(void) {
  assertCue(0x2b, GNG_PLAY, 1);
  assertCue(0x26, GNG_PLAY, 1); // other stage loops: main theme too
  assertCue(0x35, GNG_PLAY, 2);
  assertCue(0x36, GNG_PLAY, 2);
}

// death 3f 00 31, game over 3f 00 2f, player ready 30, intro 3c 3d 3a
static void test_jingles_stop(void) {
  assertCue(0x00, GNG_STOP, 0);
  assertCue(0x3f, GNG_STOP, 0);
  assertCue(0x31, GNG_STOP, 0);
  assertCue(0x2f, GNG_STOP, 0);
  assertCue(0x30, GNG_STOP, 0);
  assertCue(0x3c, GNG_STOP, 0);
}

// SSG effects stay with the sound CPU, ff is the idle write
static void test_effects_ignored(void) {
  assertCue(0x06, GNG_NONE, 0);
  assertCue(0x1b, GNG_NONE, 0);
  assertCue(0x24, GNG_NONE, 0);
  assertCue(0xff, GNG_NONE, 0);
}

// the sound driver only decodes bits 0-5
static void test_upper_bits_ignored(void) {
  assertCue(0x6b, GNG_PLAY, 1);
  assertCue(0xb1, GNG_STOP, 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_loops_play);
  RUN_TEST(test_jingles_stop);
  RUN_TEST(test_effects_ignored);
  RUN_TEST(test_upper_bits_ignored);
  return UNITY_END();
}
