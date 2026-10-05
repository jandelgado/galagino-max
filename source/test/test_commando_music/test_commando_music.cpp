#include <unity.h>

#include "../../src/machines/commando/commando_music.h"

void setUp(void) {}
void tearDown(void) {}

static void assertPlay(uint8_t command, uint8_t subtune) {
  const MusicCue cue = commandoMusicCue(command);
  TEST_ASSERT_EQUAL(MUSIC_PLAY, cue.action);
  TEST_ASSERT_EQUAL(subtune, cue.subtune);
}

// MAME trace: 20 start, 21 in-game, 22 respawn, 27 game over, 28 name
// entry, 29 after name entry
static void assertEffect(uint8_t command, uint8_t subtune) {
  const MusicCue cue = commandoMusicCue(command);
  TEST_ASSERT_EQUAL(MUSIC_EFFECT, cue.action);
  TEST_ASSERT_EQUAL(subtune, cue.subtune);
}

static void test_cue_music(void) {
  assertPlay(0x20, 1);
  assertPlay(0x21, 1);
  assertPlay(0x22, 1);
  assertPlay(0x28, 2);
  assertPlay(0x29, 3);
}

// not reached by the trace, roles from the main ROM
static void test_cue_untraced_music(void) {
  assertPlay(0x23, 1);
  assertPlay(0x24, 1);
  assertPlay(0x26, 1);
  assertPlay(0x2c, 1);
  assertPlay(0x25, 3);
  assertPlay(0x2a, 3);
}

static void test_cue_stop(void) {
  TEST_ASSERT_EQUAL(MUSIC_STOP, commandoMusicCue(0x2d).action);
}

// played over the music, like the C64 game
static void test_cue_effects(void) {
  assertEffect(0x18, 4); // grenade pickup
  assertEffect(0x1b, 16); // bonus life
  assertEffect(0x2b, 8); // player death
  assertEffect(0x27, 8); // game over
}


// SSG effects, idle 0xff
static void test_cue_unknown_ignored(void) {
  const uint8_t ignored[] = {0x00, 0x03, 0x05, 0x0f, 0x10, 0x11, 0x12, 0x1a, 0x2e, 0xff};
  for (uint8_t c : ignored) {
    TEST_ASSERT_EQUAL_MESSAGE(MUSIC_NONE, commandoMusicCue(c).action, "effect must not touch music");
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_cue_music);
  RUN_TEST(test_cue_untraced_music);
  RUN_TEST(test_cue_stop);
  RUN_TEST(test_cue_effects);
  RUN_TEST(test_cue_unknown_ignored);
  return UNITY_END();
}
