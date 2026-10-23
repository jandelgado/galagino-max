#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots Mikie on the host through the machine class and checks that the
// main CPU's task queue wait loop is skipped, not stepped. ROM headers come
// from romconv (./convert.sh mikie); without them the test is skipped.
#if __has_include("../../src/machines/mikie/mikie_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_MIKIE
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
// counts the instructions the machine asks the main CPU to run
static uint32_t stepped;
static int counted_step(m6809_state *s, char count) {
  stepped += count;
  return m6809_step(s, count);
}
#define m6809_step counted_step
#define private public
#define protected public
#include "../../src/machines/mikie/mikie.cpp"
#undef private
#undef protected
#undef m6809_step
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static mikie *machine;
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }
extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static const uint32_t ATTRACT_FRAMES = 1800, FRAMES = 600;
static const uint32_t STEPPED_BEFORE = 6594; // main CPU instructions per frame, loop stepped
static const uint32_t STEPPED_MAX = 4800;

// the main CPU spends most of the frame in its wait loop: skipped, far
// fewer instructions are stepped (6594 per frame when stepped)
static void test_idle_loop_skipped(void) {
  Arena::init();
  machine = new mikie();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
  for (uint32_t f = 0; f < ATTRACT_FRAMES; f++) {
    machine->run_frame();
  }
  stepped = 0;
  for (uint32_t f = 0; f < FRAMES; f++) {
    machine->run_frame();
  }
  printf("main instructions per frame %u (stepped loop: %u)\n", stepped / FRAMES, STEPPED_BEFORE);
  TEST_ASSERT_LESS_THAN_UINT32(STEPPED_MAX, stepped / FRAMES);
}

// a skipped pass is charged IDLE_PASS_CYCLES: one real pass of a copy of
// the idle CPU takes exactly that and returns to the loop start
static void test_idle_pass_cycles(void) {
  uint32_t checked = 0;
  for (uint32_t f = 0; f < FRAMES && checked < 10; f++) {
    machine->run_frame();
    for (int i = 0; i < 5000 && !machine->main_idle(); i++) {
      m6809_step(&machine->main_cpu, 1);
    }
    if (!machine->main_idle()) {
      continue;
    }
    m6809_state copy = machine->main_cpu;
    int cycles = 0;
    for (int i = 0; i < IDLE_PASS_INSTR; i++) {
      cycles += m6809_step(&copy, 1);
    }
    TEST_ASSERT_EQUAL_HEX16(IDLE_PC, copy.PC);
    TEST_ASSERT_EQUAL_INT(IDLE_PASS_CYCLES, cycles);
    checked++;
  }
  printf("idle passes checked %u\n", checked);
  TEST_ASSERT_GREATER_THAN_UINT32(0, checked);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_idle_loop_skipped);
  RUN_TEST(test_idle_pass_cycles);
#endif
  return UNITY_END();
}
