#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Gottlieb sound board (gottlieb_sound.cpp) against MAME: fed the main
// CPU's sound commands at their MAME cycles, the 6502 must write the same
// DAC, Votrax and speech clock values in the same order (mame_sound.h, 22 s
// of attract incl. boot speech). ROM headers from romconv (./convert.sh
// qbert); without them the test is skipped.
#if __has_include("../../src/machines/qbert/qbert_rom.h")
#define HAVE_ROM 1
#define GOTTLIEB_SOUND_TRACE
#define ENABLE_QBERT
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/votrax.cpp"
#define private public
#include "../../src/machines/qbert/gottlieb_sound.cpp"
#undef private
#include "../../src/machines/qbert/qbert_rom.h"
#include "mame_sound.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int MAX_WRITES = 20000;
static SoundEvent host[MAX_WRITES];
static int hostCount;

static void record(uint32_t cycle, uint16_t addr, uint8_t value) {
  if (cycle >= MAME_SOUND_START && hostCount < MAX_WRITES) {
    host[hostCount++] = {cycle, (uint16_t)((addr >> 12) << 8 | value)};
  }
}

static uint8_t table[VotraxSc01::TABLE_BYTES] __attribute__((aligned(4)));
static GottliebSound board;

// opcode fetches at the command wait loop (0x7035), see GottliebSound::skipIdle
static uint32_t idleFetches;
static uint8_t countFetch(m6502_t *c, uint16_t a) {
  idleFetches += (a & 0x7fff) == 0x7035;
  return GottliebSound::read(c, a);
}

static void test_writes_match_mame(void) {
  Arena::init();
  Asset<unsigned char, COMPRESSED> rom(qbert_sound_rom_blob);
  Asset<unsigned char, COMPRESSED> sc01(qbert_sc01_blob);
  board.attach(rom.data(), sc01.data(), table);
  board.reset();
  board.trace = record;
  board.cpu.fetch = countFetch;

  uint32_t now = 0;
  const int ncmd = sizeof(MAME_COMMANDS) / sizeof(MAME_COMMANDS[0]);
  for (int i = 0; i < ncmd; i++) {
    board.run(MAME_COMMANDS[i].cycle - now);
    now = MAME_COMMANDS[i].cycle;
    board.command((uint8_t)MAME_COMMANDS[i].value);
  }
  board.run(MAME_SOUND_END - now);
  while (board.sample() != GottliebSound::EMPTY) {
  }

  const int nm = sizeof(MAME_WRITES) / sizeof(MAME_WRITES[0]);
  int32_t maxErr = 0;
  int n = hostCount < nm ? hostCount : nm;
  for (int i = 0; i < n; i++) {
    if (host[i].value != MAME_WRITES[i].value) {
      printf("write %d: host %03x at %u, mame %03x at %u\n", i, host[i].value, (unsigned)host[i].cycle,
             MAME_WRITES[i].value, (unsigned)MAME_WRITES[i].cycle);
      TEST_FAIL_MESSAGE("write sequence differs");
    }
    const int32_t err = abs((int32_t)(host[i].cycle - MAME_WRITES[i].cycle));
    maxErr = err > maxErr ? err : maxErr;
  }
  printf("%d of %d writes match, max cycle error %d\n", n, nm, (int)maxErr);
  printf("wait loop passes stepped: %u\n", (unsigned)idleFetches);
  TEST_ASSERT_EQUAL_INT(nm, hostCount);
  TEST_ASSERT_LESS_THAN_INT32(64, maxErr);
  // 670629 passes without the skip; the rest run while a phone is in flight
  TEST_ASSERT_LESS_THAN_UINT32(100000, idleFetches);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_writes_match_mame);
#endif
  return UNITY_END();
}
