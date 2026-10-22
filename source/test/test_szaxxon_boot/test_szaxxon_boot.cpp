#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots the Super Zaxxon ROM (szaxxon, 315-5013 encrypted) on the host
// through the szaxxon machine class: decrypted opcode and data images,
// zaxxon board. ROM headers come from romconv (./convert.sh szaxxon);
// without them the test is skipped.
#if __has_include("../../src/machines/szaxxon/szaxxon_rom_ops.h")
#define HAVE_ROM 1
#define GAME_SZAXXON
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#define private public
#define protected public
#include "../../src/machines/zaxxon/zaxxon.cpp"
#include "../../src/machines/zaxxon/zaxxon_sound.cpp"
#include "../../src/machines/szaxxon/szaxxon.cpp"
#undef private
#undef protected
#include "mame_szaxxon_vram.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }

static szaxxon *machine;
static uint32_t frame;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opPrefixZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  Arena::reserve(&szaxxon_arena_plan);  // as main.cpp's switchTo()
  machine = new szaxxon();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// PC outside ROM (0000-4fff) and work RAM at a frame end: wrong opcodes
static uint32_t badPcFrames;

static void runTo(uint32_t f) {
  while (frame < f) {
    machine->run_frame();
    frame++;
    const uint16_t pc = machine->cpu[0].PC.W;
    badPcFrames += !(pc < 0x5000 || (pc & 0xf000) == 0x6000);
  }
}

// static attract screen: fg tiles equal MAME's at the same frame
static void test_attract_vram_matches_mame(void) {
  boot();
  runTo(180);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(mame_szaxxon_vram, machine->video_ram, sizeof(mame_szaxxon_vram));
}

// one minute of attract: title, both stages, no runaway CPU
static void test_attract_runs(void) {
  boot();
  runTo(3600);
  printf("frames with PC outside ROM/RAM: %u\n", badPcFrames);
  TEST_ASSERT_EQUAL_UINT32(0, badPcFrames);
}

static void test_difficulty_normal(void) {
  boot();
  TEST_ASSERT_EQUAL_HEX8(0x04, machine->rdZ80(0xc002) & 0x04);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_attract_vram_matches_mame);
  RUN_TEST(test_attract_runs);
  RUN_TEST(test_difficulty_normal);
#else
  printf("szaxxon ROM headers missing, run romconv\n");
#endif
  return UNITY_END();
}
