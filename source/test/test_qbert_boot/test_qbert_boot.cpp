#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Lockstep of the 8088 interpreter (cpus/i8086) against MAME:
// SP and CRC32 of RAM 0000-2fff + video RAM at every NMI entry must equal the MAME
// capture (mame_ram.h, romconv/dev/qbert/ram_fixture.lua) in three runs:
// attract, 1-player game and 2-player game without stick input. ROM header
// from romconv (./convert.sh qbert); without it the test is skipped.
#if __has_include("../../src/machines/qbert/qbert_rom.h")
#define HAVE_ROM 1
#define ENABLE_QBERT
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/votrax.cpp"
#include "../../src/machines/qbert/gottlieb_sound.cpp"
#include "../../src/cpus/i8086/i8086.cpp"
#define private public
#define protected public
#include "../../src/machines/qbert/qbert.cpp"
#undef private
#undef protected
#include "mame_ram.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

extern "C" void ResetZ80(Z80 *) {}
void Reset6502(M6502 *) {}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static const int INPUT_DELAY = 1;

// one input change at a MAME NMI index (ram_fixture.lua SCRIPTS). MAME
// latches ioport changes at its next frame update: the game sees them one NMI
// after the script set them (INPUT_DELAY).
struct Step {
  int nmi;
  unsigned int keys;
};

static const Step GAME1[] = {{600, BUTTON_COIN}, {605, 0}, {660, BUTTON_START}, {665, 0}};
static const Step GAME2[] = {{600, BUTTON_COIN}, {605, 0}, {620, BUTTON_COIN}, {625, 0},
                             {680, BUTTON_B}, {685, 0}};

static uint32_t crc32(const uint8_t *p, size_t n, uint32_t c) {
  c = ~c;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
  }
  return ~c;
}

// dead stack left out, as ram_fixture.py: NMI frame and stale copies below SP
static const int STALE = 0x40, NMI_FRAME = 6;

static uint32_t ramCrc(const uint8_t *ram, uint16_t sp) {
  static uint8_t copy[0x3000];
  memcpy(copy, ram, sizeof(copy));
  const int lo = sp > STALE ? sp - STALE : 0;
  const int hi = sp + NMI_FRAME < 0x3000 ? sp + NMI_FRAME : 0x3000;
  memset(copy + lo, 0, hi - lo);
  return crc32(ram + QBERT_VRAM, 0x400, crc32(copy, 0x3000, 0));
}

// one frame as ram_fixture.py diff reads it, in $TMPDIR (or /tmp)
static void dumpFrame(const uint8_t *ram, uint16_t sp, const char *name) {
  const char *dir = getenv("TMPDIR");
  char path[256];
  snprintf(path, sizeof(path), "%s/%s", dir ? dir : "/tmp", name);
  FILE *f = fopen(path, "wb");
  fwrite(&sp, 1, 2, f);
  fwrite(ram, 1, 0x3000, f);
  fwrite(ram + QBERT_VRAM, 1, 0x400, f);
  fclose(f);
}

static qbert *boot(void) {
  static bool arenaReady = false;
  if (!arenaReady) {
    Arena::init();
    arenaReady = true;
  }
  Arena::reset();   // the previous scenario's machine is gone
  qbert *m = new qbert();
  m->init(&input, frame_buffer, sprites, memory);
  m->reset();
  m->start();
  m->start();
  keys = 0;
  m->run_frame();   // reset code up to the first frame wait
  return m;
}

static void lockstep(const char *name, const uint16_t *mameSp, const uint32_t *mame, int count,
                     const Step *steps, int nsteps) {
  qbert *m = boot();

  // MAME's tap starts at its first frame_done: find which host NMI is index 0
  int offset = -1;
  for (int j = 0; j < 4 && offset < 0; j++) {
    m->enterNmi();
    if (i8086::SP() == mameSp[0] && ramCrc(memory, i8086::SP()) == mame[0]) {
      offset = j;
    } else {
      if (j == 0) {
        dumpFrame(memory, i8086::SP(), "qbert_host_frame.bin");
      }
      m->runCpu();
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(offset >= 0, "NMI 0 never matches MAME (host NMI 0 in $TMPDIR/qbert_host_frame.bin)");
  m->runCpu();

  int s = 0;
  int32_t minLeft = qbert::BUDGET;
  for (int n = 1; n < count; n++) {
    while (s < nsteps && steps[s].nmi + INPUT_DELAY == n) {
      keys = steps[s++].keys;
    }
    m->enterNmi();
    if (i8086::SP() != mameSp[n] || ramCrc(memory, i8086::SP()) != mame[n]) {
      dumpFrame(memory, i8086::SP(), "qbert_host_frame.bin");
      char msg[96];
      snprintf(msg, sizeof(msg), "%s: RAM differs at NMI %d ($TMPDIR/qbert_host_frame.bin)", name, n);
      TEST_FAIL_MESSAGE(msg);
    }
    m->runCpu();
    if (m->budgetLeft < minLeft) {
      minLeft = m->budgetLeft;
    }
  }
  printf("%s: %d NMIs match, offset %d, traps %u, budget cuts %u, max budget used %d\n", name, count,
         offset, (unsigned)m->trapCount, (unsigned)m->budgetCuts, (int)(qbert::BUDGET - minLeft));
  TEST_ASSERT_EQUAL_UINT32(0, m->trapCount);
  TEST_ASSERT_EQUAL_UINT32(0, m->budgetCuts);
  delete m;
}

// stick switches are screen diagonals: cardinals turned 45 degrees
// clockwise, diagonals direct (qbert.cpp readStick)
static void test_stick_mapping(void) {
  qbert *m = boot();
  struct Case {
    unsigned int keys;
    uint8_t in4;
  };
  const Case cases[] = {
      {0, 0},
      {BUTTON_UP, 0x04},                     // up-right
      {BUTTON_RIGHT, 0x01},                  // down-right
      {BUTTON_DOWN, 0x08},                   // down-left
      {BUTTON_LEFT, 0x02},                   // up-left
      {BUTTON_UP | BUTTON_RIGHT, 0x04},
      {BUTTON_DOWN | BUTTON_RIGHT, 0x01},
      {BUTTON_DOWN | BUTTON_LEFT, 0x08},
      {BUTTON_UP | BUTTON_LEFT, 0x02},
      {BUTTON_UP | BUTTON_DOWN, 0x04},        // impossible on a stick: one switch only
      {BUTTON_LEFT | BUTTON_RIGHT, 0x01},
      {BUTTON_FIRE, 0},
  };
  for (const Case &c : cases) {
    keys = c.keys;
    TEST_ASSERT_EQUAL_HEX8(c.in4, m->rd8(0x5804));
  }

  // IN1 active high, service switch (0x40) open
  keys = 0;
  TEST_ASSERT_EQUAL_HEX8(0x40, m->rd8(0x5801));
  keys = BUTTON_COIN | BUTTON_START;
  TEST_ASSERT_EQUAL_HEX8(0x45, m->rd8(0x5801));
  keys = BUTTON_B;
  TEST_ASSERT_EQUAL_HEX8(0x42, m->rd8(0x5801));
  keys = 0;
  delete m;
}

// a frame cut by the budget resumes after the next NMI, as on the board
static void test_budget_cut_resumes(void) {
  qbert *m = boot();
  m->enterNmi();
  TEST_ASSERT_EQUAL(qbert::CPU_BUDGET, m->cpuRun(1));
  m->enterNmi();
  TEST_ASSERT_EQUAL(qbert::CPU_WAIT, m->cpuRun(qbert::BUDGET));
  m->enterNmi();
  TEST_ASSERT_EQUAL(qbert::CPU_WAIT, m->cpuRun(qbert::BUDGET));
  delete m;
}

// 6000-ffff is ROM or empty: writes there are not I/O (5800-5fff)
static void test_rom_writes_ignored(void) {
  qbert *m = boot();
  m->wr8(0xc003, 0x20);   // would be the output latch at 0x5803
  TEST_ASSERT_EQUAL_HEX8(0, m->outputLatch);
  delete m;
}

// code outside the ROM (corrupt stack): count a trap and reset the board
static void test_trap_resets(void) {
  qbert *m = boot();
  i8086::setCS(0);
  i8086::setIP(0x4000);
  m->runCpu();
  TEST_ASSERT_EQUAL_UINT32(1, m->trapCount);
  TEST_ASSERT_EQUAL_HEX16(0xffff, i8086::CS());
  TEST_ASSERT_EQUAL_HEX16(0, i8086::IP());
  delete m;
}

static void test_lockstep_attract(void) {
  lockstep("attract", MAME_SP_ATTRACT, MAME_RAM_ATTRACT, 7200, nullptr, 0);
}

static void test_lockstep_game1(void) {
  lockstep("game1", MAME_SP_GAME1, MAME_RAM_GAME1, 7200, GAME1, 4);
}

static void test_lockstep_game2(void) {
  lockstep("game2", MAME_SP_GAME2, MAME_RAM_GAME2, 7200, GAME2, 6);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_stick_mapping);
  RUN_TEST(test_budget_cut_resumes);
  RUN_TEST(test_trap_resets);
  RUN_TEST(test_rom_writes_ignored);
  RUN_TEST(test_lockstep_attract);
  RUN_TEST(test_lockstep_game1);
  RUN_TEST(test_lockstep_game2);
#endif
  return UNITY_END();
}
