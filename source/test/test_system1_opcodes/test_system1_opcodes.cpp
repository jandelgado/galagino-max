#include <stdint.h>
#include <stdio.h>
#include <unity.h>

// System 1 opcode image (encrypted main CPU): M1 fetches below 0x8000 read
// the opcode image, data reads the data image; sound CPU and unencrypted
// games are untouched. Needs the romconv headers (./convert.sh swat flicky).
// Arduino.h / FastLED.h / esp_attr.h here are host stubs.
#if __has_include("../../src/machines/swat/swat_opcodes.h") && \
    __has_include("../../src/machines/flicky/flicky_main_rom.h")
#define HAVE_ROMS 1
#define ENABLE_SWAT
#define ENABLE_FLICKY
#endif

#ifdef HAVE_ROMS
#include "../../src/machines/system1/system1_collision.cpp"  // before Z80.h: its `word` macro
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/system1/system1.cpp"
#include "../../src/machines/swat/swat.cpp"
#include "../../src/machines/flicky/flicky.cpp"
#undef private
#undef protected

static system1 *machine;
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }

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

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

// romconv Arena plan per machine (<game>_arena_plan.h), reserved like switchTo()
template <class M> const ArenaPlan *planOf();
template <> const ArenaPlan *planOf<flicky>() { return &flicky_arena_plan; }
template <> const ArenaPlan *planOf<swat>() { return &swat_arena_plan; }

template <class M> static void boot() {
  static bool arena = false;
  delete machine;
  machine = nullptr;
  if (!arena) {
    Arena::init();
    arena = true;
  }
  Arena::reserve(planOf<M>());
  machine = new M();
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
}

// main CPU fetches below 0x8000 differ from data reads somewhere; the
// second byte after a prefix is an opcode fetch too
static void test_swat_reads_opcode_image(void) {
  boot<swat>();
  TEST_ASSERT_NOT_NULL(machine->opcodes);
  machine->current_cpu = system1::MAIN_CPU;
  int differ = 0;
  for (uint32_t a = 0; a < 0x8000; a++) {
    TEST_ASSERT_EQUAL_UINT8(machine->opcodes[a], machine->opZ80(a));
    TEST_ASSERT_EQUAL_UINT8(machine->opZ80(a), machine->opPrefixZ80(a));
    TEST_ASSERT_EQUAL_UINT8(machine->mainRom[a], machine->rdZ80(a));
    differ += machine->opZ80(a) != machine->rdZ80(a);
  }
  TEST_ASSERT_GREATER_THAN(0, differ);
  for (uint32_t a = 0x8000; a < 0xc000; a++) {
    TEST_ASSERT_EQUAL_UINT8(machine->rdZ80(a), machine->opZ80(a));
  }
}

static void test_swat_sound_cpu_plain(void) {
  boot<swat>();
  machine->current_cpu = system1::SOUND_CPU;
  for (uint32_t a = 0; a < machine->soundRomSize; a++) {
    TEST_ASSERT_EQUAL_UINT8(machine->soundRom[a], machine->opZ80(a));
  }
}

static void test_flicky_no_opcode_image(void) {
  boot<flicky>();
  TEST_ASSERT_NULL(machine->opcodes);
  machine->current_cpu = system1::MAIN_CPU;
  for (uint32_t a = 0; a < 0x8000; a++) {
    TEST_ASSERT_EQUAL_UINT8(machine->rdZ80(a), machine->opZ80(a));
  }
}
#endif

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROMS
  RUN_TEST(test_swat_reads_opcode_image);
  RUN_TEST(test_swat_sound_cpu_plain);
  RUN_TEST(test_flicky_no_opcode_image);
#endif
  return UNITY_END();
}
