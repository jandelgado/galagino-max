#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots Scooter Shooter (6809 main, Z80 sound, YM2203) on the host through
// the scotrsht machine class. ROM headers come from romconv
// (./convert.sh scotrsht); without them the test is skipped.
#if __has_include("../../src/machines/scotrsht/scotrsht_rom.h")
#define HAVE_ROM 1
#define GAME_SCOTRSHT
#define SCOTRSHT_Z80_TABLES_INCLUDED
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ym2203fm.cpp"
#include "../../src/emulation/ay8910.cpp"
#include "../../src/emulation/ym2203.cpp"
// counts the instructions the machine asks the main CPU to run
static uint32_t main_stepped;
static int counted_step(m6809_state *s, char count) {
  main_stepped += count;
  return m6809_step(s, count);
}
#define m6809_step counted_step
#define private public
#define protected public
#include "../../src/machines/scotrsht/scotrsht.cpp"
#undef private
#undef protected
#undef m6809_step
#include "mame_scotrsht_vram.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
void Reset6502(M6502 *) {}

static scotrsht *machine;
static uint32_t idle_fetches;   // sound opcode fetches at 021d (poll loop start)
static uint32_t tick_fetches;   // sound opcode fetches at 0223 (timer B tick)
static uint32_t irq_fetches;    // sound opcode fetches at 0038 (IRQ vector)

unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) {
  return machine->m6809_read_opcode(s, a);
}
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }
extern "C" {
unsigned char OpZ80_INL(unsigned short a) {
  idle_fetches += a == 0x021d;
  tick_fetches += a == 0x0223;
  irq_fetches += a == 0x0038;
  return machine->opZ80(a);
}
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint32_t frame;
static uint32_t badPcFrames;  // PC outside ROM / work RAM, either CPU

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  Arena::reserve(&scotrsht_arena_plan);  // as main.cpp's switchTo()
  machine = new scotrsht();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void runTo(uint32_t f) {
  while (frame < f) {
    machine->run_frame();
    frame++;
    const uint16_t pc = machine->main_cpu.PC;
    const uint16_t spc = machine->cpu[0].PC.W;
    const bool main_ok = pc >= 0x4000 || (pc >= 0x10c0 && pc < 0x2000);
    const bool snd_ok = spc < 0x4400;
    badPcFrames += !(main_ok && snd_ok);
  }
}

static uint32_t differing(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += a[i] != b[i];
  }
  return c;
}

// score ranking, static (MAME frames 300 and 400 equal)
static void test_ranking_vram_matches_mame(void) {
  boot();
  runTo(300);
  const uint32_t d = differing(mame_scotrsht_vram_300, machine->memory, 0x1000);
  printf("ranking VRAM bytes unlike MAME: %u\n", d);
  TEST_ASSERT_EQUAL_UINT32(0, d);
}

// gameplay demo: tiles static from 2200 to 2300 (MAME), column scroll
// moves every frame: must match within 2 frames of MAME's 2200
static void test_demo_matches_mame(void) {
  boot();
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(2198, frame);
  runTo(2198);
  int match = -99;
  uint32_t d = 0;
  for (int off = -2; off <= 2; off++) {
    if (off == 0) {
      d = differing(mame_scotrsht_vram_2200, machine->memory, 0x1000);
    }
    if (!differing(mame_scotrsht_scroll_2200, machine->memory + SC_MEM_SCROLL, 0x20)) {
      match = off;
    }
    runTo(frame + 1);
  }
  printf("demo VRAM bytes unlike MAME: %u, scroll equals MAME 2200 at host 2200%+d\n", d, match);
  TEST_ASSERT_EQUAL_UINT32(0, d);
  TEST_ASSERT_INT_WITHIN(2, 0, match);
}

// one minute of attract: title, ranking, demo, no runaway CPU
static void test_attract_runs(void) {
  boot();
  runTo(3600);
  printf("frames with PC outside ROM/RAM: %u\n", badPcFrames);
  TEST_ASSERT_EQUAL_UINT32(0, badPcFrames);
  TEST_ASSERT_EQUAL_UINT8(1, machine->game_started);
}

// MAME trace: 3659 of 5287 sound instructions per frame in the timer B
// poll (1220 passes). Skipped: one stepped pass per tick, 7.4 per frame; timer B still ticks at 444 Hz
// (6912 cycles), 444 ticks per 60 frames: music tempo.
static void test_sound_idle_skipped(void) {
  boot();
  idle_fetches = tick_fetches = 0;
  const uint32_t frames = 60;
  const uint32_t from = frame;
  runTo(from + frames);
  printf("sound idle fetches per frame %u, timer B ticks in %u frames %u\n",
         idle_fetches / frames, frames, tick_fetches);
  TEST_ASSERT_LESS_THAN_UINT32(10, idle_fetches / frames);
  TEST_ASSERT_UINT32_WITHIN(1, 444, tick_fetches);
}

// A latch written while the sound CPU sits in a skipped poll with
// interrupts on is taken in the next sound slice: the IRQ handler reads
// the command into its slots. The handler returns with interrupts off:
// a second latch waits, still skipped, for the EI after the next tick.
static void test_latch_irq_in_skip(void) {
  boot();
  uint32_t checked = 0, waited = 0;
  for (int tries = 0; tries < 2000 && checked < 10; tries++) {
    machine->run_sound(scotrsht::SLICE_CYCLES / 2);
    if (!machine->sound_idle() || !(machine->cpu[0].IFF & IFF_1)) {
      continue;
    }
    // inside the skip: the poll is not stepped
    idle_fetches = 0;
    machine->run_sound(scotrsht::SLICE_CYCLES / 4);
    if (!machine->sound_idle()) {
      continue;
    }
    TEST_ASSERT_EQUAL_UINT32(0, idle_fetches);

    // command 0x0b (MAME demo sends 0b-0d), slots 4000-4003 empty
    memset(machine->memory + SC_MEM_SND, 0, 4);
    irq_fetches = 0;
    machine->m6809_write(&machine->main_cpu, 0x3100, 0x0b);
    machine->run_sound(scotrsht::SLICE_CYCLES);
    TEST_ASSERT_EQUAL_UINT32(1, irq_fetches);
    TEST_ASSERT_FALSE(machine->latch_pending);
    machine->run_sound(4 * scotrsht::SLICE_CYCLES);  // handler done
    const uint8_t *slots = machine->memory + SC_MEM_SND;
    TEST_ASSERT_TRUE(slots[0] == 0x0b || slots[2] == 0x0b);
    checked++;

    // back in the poll with interrupts off: skipped, IRQ after the EI
    if (machine->cpu[0].PC.W != IDLE_PC || (machine->cpu[0].IFF & IFF_1)) {
      continue;
    }
    idle_fetches = irq_fetches = 0;
    machine->m6809_write(&machine->main_cpu, 0x3100, 0x0c);
    while (!(machine->cpu[0].IFF & IFF_1) && irq_fetches == 0) {
      machine->run_sound(scotrsht::SLICE_CYCLES);
    }
    machine->run_sound(scotrsht::SLICE_CYCLES);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1, idle_fetches);  // the pass that sees the flag
    TEST_ASSERT_EQUAL_UINT32(1, irq_fetches);
    TEST_ASSERT_FALSE(machine->latch_pending);
    waited++;
  }
  printf("latch writes inside the skip checked: %u, waiting for EI: %u\n", checked, waited);
  TEST_ASSERT_EQUAL_UINT32(10, checked);
  TEST_ASSERT_GREATER_THAN_UINT32(0, waited);
}

// a skipped pass is charged IDLE_PASS_CYCLES: one real pass takes that
static void test_idle_pass_cycles(void) {
  boot();
  for (int i = 0; i < 100000 && machine->cpu[0].PC.W != IDLE_PC; i++) {
    machine->ym.tick(machine->step_sound());
  }
  TEST_ASSERT_EQUAL_HEX16(IDLE_PC, machine->cpu[0].PC.W);
  int cycles = 0;
  for (int i = 0; i < 3; i++) {
    cycles += machine->step_sound();
  }
  if (machine->cpu[0].PC.W == IDLE_PC) {
    TEST_ASSERT_EQUAL_INT(IDLE_PASS_CYCLES, cycles);
  }
}

// Main CPU task queue wait loop: 6 instructions, 26 cycles per pass.
// Stepped: MAIN_STEPPED_BEFORE instructions per frame (host profile).
static const uint32_t MAIN_STEPPED_BEFORE = 11844, MAIN_STEPPED_MAX = 6000;

static void test_main_idle_skipped(void) {
  boot();
  main_stepped = 0;
  const uint32_t frames = 600;
  runTo(frame + frames);
  printf("main instructions per frame %u (stepped loop: %u)\n", main_stepped / frames,
         MAIN_STEPPED_BEFORE);
  TEST_ASSERT_LESS_THAN_UINT32(MAIN_STEPPED_MAX, main_stepped / frames);
}

// a skipped pass is charged MAIN_PASS_CYCLES: one real pass of a copy of
// the idle CPU takes exactly that and returns to the loop start
static void test_main_pass_cycles(void) {
  boot();
  uint32_t checked = 0;
  for (uint32_t f = 0; f < 600 && checked < 10; f++) {
    runTo(frame + 1);
    machine->run_main(scotrsht::SLICE_CYCLES * 8);  // vblank IRQ handled
    for (int i = 0; i < 20000 && !machine->main_idle(); i++) {
      m6809_step(&machine->main_cpu, 1);
    }
    if (!machine->main_idle()) {
      continue;
    }
    m6809_state copy = machine->main_cpu;
    int cycles = 0;
    for (int i = 0; i < 6; i++) {
      cycles += m6809_step(&copy, 1);
    }
    TEST_ASSERT_EQUAL_HEX16(MAIN_IDLE_PC, copy.PC);
    TEST_ASSERT_EQUAL_INT(MAIN_PASS_CYCLES, cycles);
    checked++;
  }
  printf("main idle passes checked %u\n", checked);
  TEST_ASSERT_GREATER_THAN_UINT32(0, checked);
}

static void test_dsw(void) {
  boot();
  TEST_ASSERT_EQUAL_HEX8(0x7e, machine->m6809_read(&machine->main_cpu, 0x3100));
  TEST_ASSERT_EQUAL_HEX8(0x0f, machine->m6809_read(&machine->main_cpu, 0x3200));
  TEST_ASSERT_EQUAL_HEX8(0xff, machine->m6809_read(&machine->main_cpu, 0x3303));
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_ranking_vram_matches_mame);
  RUN_TEST(test_demo_matches_mame);
  RUN_TEST(test_attract_runs);
  RUN_TEST(test_sound_idle_skipped);
  RUN_TEST(test_latch_irq_in_skip);
  RUN_TEST(test_idle_pass_cycles);
  RUN_TEST(test_main_idle_skipped);
  RUN_TEST(test_main_pass_cycles);
  RUN_TEST(test_dsw);
#else
  printf("scotrsht ROM headers missing, run romconv\n");
#endif
  return UNITY_END();
}
