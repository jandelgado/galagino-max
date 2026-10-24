#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Lockstep of the 8088 interpreter (cpus/i8086) against MAME: SP and CRC32 of
// RAM 0000-2fff + video RAM at every NMI (after the handler's pushes) must
// equal the MAME capture (mame_ram.h, romconv/dev/tylz/ram_fixture.lua) in
// attract and a 1-player game without stick input. NMIs that MAME lands
// outside the frame wait (logic still running: needs cycle timing the
// interpreter does not model) are skipped and counted; the next NMI matches
// again. Attract stops at NMI 805: MAME spends 4 frames on the next level's
// setup there, the interpreter (instructions, not cycles) fewer, and the
// game stays a frame or two ahead from then on. ROM header from romconv (./convert.sh tylz); without it the test is
// skipped.
#if __has_include("../../src/machines/tylz/tylz_rom.h")
#define HAVE_ROM 1
#define ENABLE_TYLZ
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/votrax.cpp"
#include "../../src/machines/gottlieb/gottlieb_sound.cpp"
#include "../../src/cpus/i8086/i8086.cpp"
#define private public
#define protected public
#include "../../src/machines/gottlieb/gottlieb_machine.cpp"
#include "../../src/machines/tylz/tylz.cpp"
#undef private
#undef protected
#include "mame_ram.h"
#include "../test_tylz_video/mame_frames.h"
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
static const int NMIS = 1800;
static const int ATTRACT_NMIS = 805;
static const int BOOT_NMIS = 150;   // past the boot ROM checksum
static const uint16_t WAIT_FIRST = 0x8aae, WAIT_LAST = 0x8ab3;
// after the NMI handler's 7 pushes, before "mov es,ax; mov ds,ax; mov [0C11h],0FFh":
// the interpreter steps a move to a segment register together with the next
// instruction (interrupt shadow), so it never stops at MAME's tap (0x8753)
static const uint16_t HANDLER_BODY = 0x874f;

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

// masked as ram_fixture.py: pushed registers, NMI frame and stale copies below SP
static const int STALE = 0x40, PUSHED = 14, NMI_FRAME = 6;

static uint32_t ramCrc(const uint8_t *ram, uint16_t sp) {
  static uint8_t copy[TYLZ_RAM_END];
  memcpy(copy, ram, sizeof(copy));
  const int lo = sp > STALE ? sp - STALE : 0;
  const int hi = sp + PUSHED + NMI_FRAME < TYLZ_RAM_END ? sp + PUSHED + NMI_FRAME : TYLZ_RAM_END;
  memset(copy + lo, 0, hi - lo);
  return crc32(ram + TYLZ_VRAM, 0x400, crc32(copy, TYLZ_RAM_END, 0));
}

// one frame as ram_fixture.py diff reads it, in $TMPDIR (or /tmp)
static void dumpFrame(const uint8_t *ram, uint16_t sp, const char *name) {
  const char *dir = getenv("TMPDIR");
  char path[256];
  snprintf(path, sizeof(path), "%s/%s", dir ? dir : "/tmp", name);
  FILE *f = fopen(path, "wb");
  fwrite(&sp, 1, 2, f);
  fwrite(ram, 1, TYLZ_RAM_END, f);
  fwrite(ram + TYLZ_VRAM, 1, 0x400, f);
  fclose(f);
}

static tylz *boot(void) {
  static bool arenaReady = false;
  if (!arenaReady) {
    Arena::init();
    arenaReady = true;
  }
  Arena::reset();   // the previous scenario's machine is gone
  tylz *m = new tylz();
  m->init(&input, frame_buffer, sprites, memory);
  m->reset();
  m->start();
  m->start();
  keys = 0;
  m->run_frame();   // reset code up to the first frame wait
  return m;
}

// NMI, then the handler's pushes: where MAME's tap samples
static void nmiToBody(tylz *m) {
  m->enterNmi();
  for (int i = 0; i < 16 && i8086::IP() != HANDLER_BODY; i++) {
    i8086::step();
  }
}

static bool matches(int n, const uint16_t *mameSp, const uint32_t *mame) {
  return i8086::SP() == mameSp[n] && ramCrc(memory, i8086::SP()) == mame[n];
}

static void lockstep(const char *name, int nmis, const uint16_t *mameSp, const uint16_t *mameIp,
                     const uint32_t *mame, const Step *steps, int nsteps) {
  tylz *m = boot();

  // MAME's tap starts at its first frame_done: find which host NMI is index 0
  int offset = -1;
  for (int j = 0; j < 4 && offset < 0; j++) {
    nmiToBody(m);
    if (matches(0, mameSp, mame)) {
      offset = j;
    } else {
      if (j == 0) {
        dumpFrame(memory, i8086::SP(), "tylz_host_frame.bin");
      }
      m->runCpu();
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(offset >= 0, "NMI 0 never matches MAME (host NMI 0 in $TMPDIR/tylz_host_frame.bin)");
  m->runCpu();

  int s = 0, outside = 0, differ = 0;
  int32_t minLeft = tylz::BUDGET;
  for (int n = 1; n < nmis; n++) {
    while (s < nsteps && steps[s].nmi + INPUT_DELAY == n) {
      keys = steps[s++].keys;
    }
    nmiToBody(m);
    const bool waiting = mameIp[n] >= WAIT_FIRST && mameIp[n] <= WAIT_LAST;
    outside += !waiting;
    if (!matches(n, mameSp, mame)) {
      if (!waiting) {
        differ++;
      } else {
        dumpFrame(memory, i8086::SP(), "tylz_host_frame.bin");
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: RAM differs at NMI %d ($TMPDIR/tylz_host_frame.bin)", name, n);
        TEST_FAIL_MESSAGE(msg);
      }
    }
    m->runCpu();
    if (m->budgetLeft < minLeft) {
      minLeft = m->budgetLeft;
    }
  }
  printf("%s: %d NMIs match, %d outside the frame wait skipped (%d differ), offset %d, traps %u, budget cuts %u, "
         "max budget used %d\n",
         name, nmis - outside, outside, differ, offset, (unsigned)m->trapCount, (unsigned)m->budgetCuts,
         (int)(tylz::BUDGET - minLeft));
  TEST_ASSERT_EQUAL_UINT32(0, m->trapCount);
  delete m;
}

// IN4 active high: 8-way stick, button 1, start 1/2, b7 dip open (MAME tylz ports)
static void test_inputs(void) {
  tylz *m = boot();
  struct Case {
    unsigned int keys;
    uint8_t in4;
  };
  const Case cases[] = {
      {0, 0x80},
      {BUTTON_UP, 0x84},
      {BUTTON_DOWN, 0x81},
      {BUTTON_LEFT, 0x88},
      {BUTTON_RIGHT, 0x82},
      {BUTTON_UP | BUTTON_RIGHT, 0x86},
      {BUTTON_FIRE, 0x90},
      {BUTTON_START, 0xa0},
      {BUTTON_B, 0xc0},
  };
  for (const Case &c : cases) {
    keys = c.keys;
    TEST_ASSERT_EQUAL_HEX8(c.in4, m->rd8(0x5804));
  }

  // IN1 active high, service switch (0x01) open; DSW demo sounds on
  keys = 0;
  TEST_ASSERT_EQUAL_HEX8(0x01, m->rd8(0x5801));
  keys = BUTTON_COIN;
  TEST_ASSERT_EQUAL_HEX8(0x09, m->rd8(0x5801));
  TEST_ASSERT_EQUAL_HEX8(0x08, m->rd8(0x5800));
  keys = 0;
  delete m;
}

// the attract loop reaches the frame wait at almost every NMI
static void test_frame_wait_hit(void) {
  tylz *m = boot();
  int waits = 0;
  for (int n = 0; n < 600; n++) {
    m->enterNmi();
    waits += m->cpuRun(tylz::BUDGET) == tylz::CPU_WAIT;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_INT(570, waits);
  delete m;
}

// a frame cut by the budget resumes after the next NMI, as on the board
// (after the boot ROM checksum, which spans several NMIs)
static void test_budget_cut_resumes(void) {
  tylz *m = boot();
  for (int n = 0; n < BOOT_NMIS; n++) {
    m->run_frame();
  }
  m->enterNmi();
  TEST_ASSERT_EQUAL(tylz::CPU_BUDGET, m->cpuRun(1));
  m->enterNmi();
  TEST_ASSERT_EQUAL(tylz::CPU_WAIT, m->cpuRun(tylz::BUDGET));
  m->enterNmi();
  TEST_ASSERT_EQUAL(tylz::CPU_WAIT, m->cpuRun(tylz::BUDGET));
  delete m;
}

// 6000-7fff is empty: reads 0, writes are not I/O
static void test_empty_sockets(void) {
  tylz *m = boot();
  TEST_ASSERT_EQUAL_HEX8(0, m->rd8(0x6000));
  TEST_ASSERT_EQUAL_HEX8(0, m->rd8(0x7fff));
  m->wr8(0x6003, 0x20);   // would be the output latch at 0x5803
  TEST_ASSERT_EQUAL_HEX8(0, m->outputLatch);
  delete m;
}

// render input the game writes (video RAM, sprites, palette, 0x5803) equals
// MAME's at the video fixture frames (frame_done = before the next vblank
// NMI: the host latches the same moment in run_frame()); frame k of MAME is
// host run_frame() k + the frame offset found on the first fixture frame
static void test_video_input_matches_mame(void) {
  static const int SEARCH = 8;      // host frames tried around the align frame
  static const int ALIGN = 1;       // fixture frame 220: title palette fading, one value per frame
  static const int COIN_FRAME = 600;   // video_fixture.lua inserts a coin here
  tylz *m = boot();
  static GottliebVideoInput v;
  const int frames = sizeof(MAME_FRAMES) / sizeof(MAME_FRAMES[0]);

  // align: host run_frame() count whose latch equals MAME's ALIGN frame
  const MameFrame &a = MAME_FRAMES[ALIGN];
  int hostFrame = 1, offset = -SEARCH - 1;
  for (; hostFrame < a.frame - SEARCH; hostFrame++) {
    m->run_frame();
  }
  for (int k = -SEARCH; k <= SEARCH; k++, hostFrame++) {
    m->latchVideo(v);
    if (!memcmp(v.vram, a.vram, sizeof(v.vram)) && !memcmp(v.spr, a.spr, sizeof(v.spr)) &&
        !memcmp(v.pal, a.pal, sizeof(v.pal))) {
      offset = k;
      break;
    }
    m->run_frame();
  }
  printf("video input: offset %d\n", offset);
  TEST_ASSERT_TRUE_MESSAGE(offset >= -SEARCH, "first fixture frame never matches");

  int bad = 0;
  for (int i = ALIGN + 1; i < frames && MAME_FRAMES[i].frame < COIN_FRAME; i++) {
    const MameFrame &f = MAME_FRAMES[i];
    for (; hostFrame < f.frame + offset; hostFrame++) {
      m->run_frame();
    }
    m->latchVideo(v);
    const bool vramOk = !memcmp(v.vram, f.vram, sizeof(v.vram));
    const bool sprOk = !memcmp(v.spr, f.spr, sizeof(v.spr));
    const bool palOk = !memcmp(v.pal, f.pal, sizeof(v.pal));
    if (vramOk && sprOk && palOk && v.ctrl == f.ctrl) {
      continue;
    }
    printf("frame %d: vram %d spr %d pal %d ctrl %02x/%02x\n", f.frame, vramOk, sprOk, palOk, v.ctrl, f.ctrl);
    for (int p = 0; p < 0x20 && !palOk; p++) {
      printf("%02x/%02x ", v.pal[p], f.pal[p]);
    }
    printf("\n");
    bad++;
  }
  TEST_ASSERT_EQUAL_INT(0, bad);
  delete m;
}

// code outside the ROM (corrupt stack): count a trap and reset the board
static void test_trap_resets(void) {
  tylz *m = boot();
  i8086::setCS(0);
  i8086::setIP(0x4000);
  m->runCpu();
  TEST_ASSERT_EQUAL_UINT32(1, m->trapCount);
  TEST_ASSERT_EQUAL_HEX16(0xffff, i8086::CS());
  TEST_ASSERT_EQUAL_HEX16(0, i8086::IP());
  delete m;
}

static void test_attract_lockstep(void) {
  lockstep("attract", ATTRACT_NMIS, MAME_SP_ATTRACT, MAME_IP_ATTRACT, MAME_RAM_ATTRACT, nullptr, 0);
}

static void test_game1_lockstep(void) {
  lockstep("game1", NMIS, MAME_SP_GAME1, MAME_IP_GAME1, MAME_RAM_GAME1, GAME1, 4);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_inputs);
  RUN_TEST(test_frame_wait_hit);
  RUN_TEST(test_budget_cut_resumes);
  RUN_TEST(test_empty_sockets);
  RUN_TEST(test_trap_resets);
  RUN_TEST(test_video_input_matches_mame);
  RUN_TEST(test_attract_lockstep);
  RUN_TEST(test_game1_lockstep);
#endif
  return UNITY_END();
}
