#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Lockstep of the 8088 interpreter (cpus/i8086) against MAME: SP and CRC32 of
// RAM 0000-0fff + video RAM + char RAM at every NMI entry must equal the MAME
// capture (mame_ram.h, romconv/dev/krull/ram_fixture.lua) in attract and a
// 1-player game without stick input. NMIs that MAME lands outside the idle
// loop (logic still running: needs cycle timing the interpreter does not
// model, MAME_OUTSIDE_*) are skipped and counted; the next NMI matches again. ROM header
// from romconv (./convert.sh krull); without it the test is skipped.
#if __has_include("../../src/machines/krull/krull_rom.h")
#define HAVE_ROM 1
#define ENABLE_KRULL
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/votrax.cpp"
#include "../../src/machines/gottlieb/gottlieb_sound.cpp"
#include "../../src/cpus/i8086/i8086.cpp"
#define private public
#define protected public
#include "../../src/machines/gottlieb/gottlieb_machine.cpp"
#include "../../src/machines/krull/krull.cpp"
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

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static const int INPUT_DELAY = 1;
static const int NMIS = 7200;

// one input change at a MAME NMI index (ram_fixture.lua SCRIPTS). MAME
// latches ioport changes at its next frame update: the game sees them one NMI
// after the script set them (INPUT_DELAY).
struct Step {
  int nmi;
  unsigned int keys;
};

static const Step GAME1[] = {{600, BUTTON_COIN}, {605, 0}, {660, BUTTON_START}, {665, 0}};

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

// masked as ram_fixture.py: NMI frame and stale copies below SP, idle pass count
static const int STALE = 0x40, NMI_FRAME = 6;
static const uint16_t IDLE_COUNT = 0x9e9;

static uint32_t ramCrc(const uint8_t *ram, uint16_t sp) {
  static uint8_t copy[KRULL_RAM_END];
  memcpy(copy, ram, sizeof(copy));
  const int lo = sp > STALE ? sp - STALE : 0;
  const int hi = sp + NMI_FRAME < KRULL_RAM_END ? sp + NMI_FRAME : KRULL_RAM_END;
  memset(copy + lo, 0, hi - lo);
  memset(copy + IDLE_COUNT, 0, 2);
  const uint32_t c = crc32(copy, sizeof(copy), 0);
  return crc32(ram + KRULL_CHARRAM, 0x1000, crc32(ram + KRULL_VRAM, 0x400, c));
}

// one frame as ram_fixture.py diff reads it, in $TMPDIR (or /tmp)
static void dumpFrame(const uint8_t *ram, uint16_t sp, const char *name) {
  const char *dir = getenv("TMPDIR");
  char path[256];
  snprintf(path, sizeof(path), "%s/%s", dir ? dir : "/tmp", name);
  FILE *f = fopen(path, "wb");
  fwrite(&sp, 1, 2, f);
  fwrite(ram, 1, KRULL_RAM_END, f);
  fwrite(ram + KRULL_VRAM, 1, 0x400, f);
  fwrite(ram + KRULL_CHARRAM, 1, 0x1000, f);
  fclose(f);
}

static krull *boot(void) {
  static bool arenaReady = false;
  if (!arenaReady) {
    Arena::init();
    arenaReady = true;
  }
  Arena::reset();   // the previous scenario's machine is gone
  krull *m = new krull();
  m->init(&input, frame_buffer, sprites, memory);
  m->reset();
  m->start();
  m->start();
  keys = 0;
  m->run_frame();   // reset code up to the first frame wait
  return m;
}

static bool matches(int n, const uint16_t *mameSp, const uint32_t *mame) {
  return i8086::SP() == mameSp[n] && ramCrc(memory, i8086::SP()) == mame[n];
}

static bool isOutside(int n, const uint16_t *outside, int noutside) {
  for (int i = 0; i < noutside; i++) {
    if (outside[i] == n) {
      return true;
    }
  }
  return false;
}

static void lockstep(const char *name, const uint16_t *mameSp, const uint16_t *outside, int noutside,
                     const uint32_t *mame, const Step *steps, int nsteps) {
  krull *m = boot();

  // MAME's tap starts at its first frame_done: find which host NMI is index 0
  int offset = -1;
  for (int j = 0; j < 4 && offset < 0; j++) {
    m->enterNmi();
    if (matches(0, mameSp, mame)) {
      offset = j;
    } else {
      if (j == 0) {
        dumpFrame(memory, i8086::SP(), "krull_host_frame.bin");
      }
      m->runCpu();
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(offset >= 0, "NMI 0 never matches MAME (host NMI 0 in $TMPDIR/krull_host_frame.bin)");
  m->runCpu();

  int s = 0, skipped = 0, differ = 0;
  int32_t minLeft = krull::BUDGET;
  for (int n = 1; n < NMIS; n++) {
    while (s < nsteps && steps[s].nmi + INPUT_DELAY == n) {
      keys = steps[s++].keys;
    }
    m->enterNmi();
    const bool idle = !isOutside(n, outside, noutside);
    skipped += !idle;
    if (!matches(n, mameSp, mame)) {
      if (!idle) {
        differ++;
      } else {
        dumpFrame(memory, i8086::SP(), "krull_host_frame.bin");
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: RAM differs at NMI %d ($TMPDIR/krull_host_frame.bin)", name, n);
        TEST_FAIL_MESSAGE(msg);
      }
    }
    m->runCpu();
    if (m->budgetLeft < minLeft) {
      minLeft = m->budgetLeft;
    }
  }
  printf("%s: %d NMIs match, %d outside the idle loop skipped (%d differ), offset %d, traps %u, budget cuts %u, "
         "max budget used %d\n",
         name, NMIS - skipped, skipped, differ, offset, (unsigned)m->trapCount, (unsigned)m->budgetCuts,
         (int)(krull::BUDGET - minLeft));
  TEST_ASSERT_EQUAL_UINT32(0, m->trapCount);
  TEST_ASSERT_EQUAL_UINT32(0, m->budgetCuts);
  delete m;
}

static void test_attract_lockstep(void) {
  lockstep("attract", MAME_SP_ATTRACT, MAME_OUTSIDE_ATTRACT, sizeof(MAME_OUTSIDE_ATTRACT) / 2, MAME_RAM_ATTRACT,
           nullptr, 0);
}

static void test_game1_lockstep(void) {
  lockstep("game1", MAME_SP_GAME1, MAME_OUTSIDE_GAME1, sizeof(MAME_OUTSIDE_GAME1) / 2, MAME_RAM_GAME1, GAME1,
           4);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_attract_lockstep);
  RUN_TEST(test_game1_lockstep);
#endif
  return UNITY_END();
}
