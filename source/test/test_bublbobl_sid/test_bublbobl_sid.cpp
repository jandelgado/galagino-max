#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/sid.cpp"
#include "../../src/emulation/sidplayer.cpp"

// romszip/Bubble_Bobble.sid (HVSC Clarke_Peter, user supplied, not in
// the repo): the tune must run inside bublbobl's RAM window
static const uint16_t LOAD = 0xEA40;
static const uint16_t TUNE_RAM_SIZE = 0x10000 - LOAD; // bublbobl.h
static const uint8_t SONGS = 12;
static const int SECONDS = 20;

// Stray accesses per subtune in SECONDS. All are dummy reads of $03A9 /
// $1CA9 from the 6502 "BIT abs" skip idiom at $ED33 / $ED93
// (LDA #n; .byte $2C; LDA #m): the value only sets flags nobody reads.
// No stray writes.
static const uint8_t EXPECTED_STRAYS[SONGS] = {3, 3, 6, 13, 0, 3, 3, 1, 0, 0, 1, 1};

static uint8_t tune_file[0x2000];
static uint8_t tune_ram[TUNE_RAM_SIZE];
static int16_t buf[24000];
static SidPlayer player;
static PsidInfo info;

static uint32_t loadTune(void) {
  const char *paths[] = {"../romszip/Bubble_Bobble.sid", "romszip/Bubble_Bobble.sid"};
  for (const char *p : paths) {
    FILE *f = fopen(p, "rb");
    if (!f) {
      continue;
    }
    const uint32_t len = fread(tune_file, 1, sizeof(tune_file), f);
    fclose(f);
    return len;
  }
  return 0;
}

void setUp(void) {}
void tearDown(void) {}

static void test_header(void) {
  const uint32_t len = loadTune();
  if (!len) {
    TEST_IGNORE_MESSAGE("romszip/Bubble_Bobble.sid missing");
  }
  TEST_ASSERT_TRUE(psidParse(tune_file, len, &info));
  TEST_ASSERT_EQUAL_HEX16(LOAD, info.load);
  TEST_ASSERT_EQUAL_HEX16(0xEE50, info.init);
  TEST_ASSERT_EQUAL_HEX16(0xF53C, info.play);
  TEST_ASSERT_EQUAL(SONGS, info.songs);
}

// every subtune: no access outside $0000-$01FF, the tune window and the
// SID except the known dummy reads, and some sound comes out
static void test_stays_in_window(void) {
  const uint32_t len = loadTune();
  if (!len) {
    TEST_IGNORE_MESSAGE("romszip/Bubble_Bobble.sid missing");
  }
  TEST_ASSERT_TRUE(psidParse(tune_file, len, &info));
  TEST_ASSERT_TRUE(player.load(info, tune_ram, TUNE_RAM_SIZE));

  for (uint8_t song = 1; song <= SONGS; song++) {
    player.start(song);
    int32_t peak = 0;
    for (int s = 0; s < SECONDS; s++) {
      player.render(buf, 24000);
      for (int i = 0; i < 24000; i++) {
        const int32_t v = buf[i] < 0 ? -buf[i] : buf[i];
        peak = v > peak ? v : peak;
      }
    }
    char msg[48];
    snprintf(msg, sizeof(msg), "song %d strays", song);
    TEST_ASSERT_EQUAL_MESSAGE(EXPECTED_STRAYS[song - 1], player.strays(), msg);
    snprintf(msg, sizeof(msg), "song %d silent", song);
    TEST_ASSERT_TRUE_MESSAGE(peak > 0, msg);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_header);
  RUN_TEST(test_stays_in_window);
  return UNITY_END();
}
