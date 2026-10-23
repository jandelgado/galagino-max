#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Krull's Gottlieb sound board (gottlieb_sound.cpp, no Votrax) against MAME:
// fed the main CPU's sound commands at their MAME cycles, the 6502 must
// write the same DAC values in the same order (mame_sound.h, 22 s of
// attract, CRC32 per block of writes) at about the same cycles. ROM header
// from romconv (./convert.sh krull); without it the test is skipped.
#if __has_include("../../src/machines/krull/krull_rom.h")
#define HAVE_ROM 1
#define GOTTLIEB_SOUND_TRACE
#define ENABLE_KRULL
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/votrax.cpp"
#define private public
#include "../../src/machines/gottlieb/gottlieb_sound.cpp"
#undef private
#include "../../src/machines/krull/krull_rom.h"
#include "mame_sound.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int MAX_WRITES = 40000;
static uint32_t hostCycle[MAX_WRITES];
static uint8_t hostValue[MAX_WRITES];
static int hostCount;

static void record(uint32_t cycle, uint16_t addr, uint8_t value) {
  if (cycle >= MAME_SOUND_START && addr == 0x1000 && hostCount < MAX_WRITES) {
    hostCycle[hostCount] = cycle;
    hostValue[hostCount++] = value;
  }
}

static uint32_t crc32(const uint8_t *p, int n) {
  uint32_t c = 0xffffffff;
  for (int i = 0; i < n; i++) {
    c ^= p[i];
    for (int b = 0; b < 8; b++) {
      c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
  }
  return ~c;
}

static GottliebSound board;

// opcode fetches at the command wait loop (0xf13b), see GottliebSound::skipIdle
static const uint16_t IDLE_PC = 0xf13b;
static uint32_t idleFetches, fetches;
static uint8_t countFetch(m6502_t *c, uint16_t a) {
  idleFetches += a == IDLE_PC;
  fetches++;
  return GottliebSound::read(c, a);
}

static void test_dac_writes_match_mame(void) {
  Arena::init();
  Asset<unsigned char, COMPRESSED> rom(krull_sound_rom_blob);
  GottliebSound::Config sc = {};
  sc.rom = rom.data();
  sc.romBase = 0x6000;
  sc.idlePc = IDLE_PC;
  sc.idleFlag = 0x21;
  sc.idleZero1 = 0x20;
  sc.idleZero2 = 0x23;
  sc.idlePass = 29;
  sc.dacGain = 19;
  board.attach(sc);
  board.reset();
  board.trace = record;
  board.cpu.fetch = countFetch;

  uint32_t now = 0;
  const int ncmd = sizeof(MAME_COMMANDS) / sizeof(MAME_COMMANDS[0]);
  for (int i = 0; i < ncmd; i++) {
    board.run(MAME_COMMANDS[i].cycle - now);
    now = MAME_COMMANDS[i].cycle;
    board.command(MAME_COMMANDS[i].value);
  }
  board.run(MAME_SOUND_END - now);
  while (board.sample() != GottliebSound::EMPTY) {
  }

  int32_t maxErr = 0;
  const int nb = sizeof(MAME_DAC) / sizeof(MAME_DAC[0]);
  for (int b = 0; b < nb && b * MAME_DAC_BLOCK < hostCount; b++) {
    const int i = b * MAME_DAC_BLOCK;
    const int n = hostCount - i < MAME_DAC_BLOCK ? hostCount - i : MAME_DAC_BLOCK;
    const DacBlock &m = MAME_DAC[b];
    if (crc32(hostValue + i, n) != m.crc) {
      printf("block %d (writes %d-%d): host at %u, mame at %u\n", b, i, i + n - 1, (unsigned)hostCycle[i],
             (unsigned)m.first);
      TEST_FAIL_MESSAGE("DAC values differ");
    }
    const int32_t e1 = abs((int32_t)(hostCycle[i] - m.first));
    const int32_t e2 = abs((int32_t)(hostCycle[i + n - 1] - m.last));
    maxErr = e1 > maxErr ? e1 : maxErr;
    maxErr = e2 > maxErr ? e2 : maxErr;
  }
  printf("%d of %d DAC writes, max cycle error %d\n", hostCount, MAME_DAC_WRITES, (int)maxErr);
  printf("wait loop passes stepped: %u, opcodes %u\n", (unsigned)idleFetches, (unsigned)fetches);
  TEST_ASSERT_EQUAL_INT(MAME_DAC_WRITES, hostCount);
  TEST_ASSERT_LESS_THAN_INT32(64, maxErr);
  // 574807 passes without the skip
  TEST_ASSERT_LESS_THAN_UINT32(1000, idleFetches);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_dac_writes_match_mame);
#endif
  return UNITY_END();
}
