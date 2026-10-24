#include <stdio.h>
#include <string.h>
#include <unity.h>

// Runs the natively implemented alpha routines (mhavoc::native_alpha)
// against the interpreter from random states: same registers, flags,
// memory and cycles. ROM headers come from romconv (./convert.sh mhavoc);
// without them the test is skipped. Arduino.h / FastLED.h / esp_attr.h
// are host stubs.
#if __has_include("../../src/machines/mhavoc/mhavoc_rom.h")
#define HAVE_ROM 1
#define GAME_MHAVOC
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/avg.cpp"
#include "../../src/emulation/dvg.cpp"
#include "../../src/emulation/pokey.cpp"
#include "../../src/machines/bwidow/bwidow_layout.cpp"
#define private public
#define protected public
#include "../../src/machines/dvgMachine.cpp"
#include "../../src/machines/mhavoc/mhavoc_layout.cpp"
#include "../../src/machines/mhavoc/mhavoc.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}

static mhavoc *machine;
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE], saved[RAMSIZE], interpreted[RAMSIZE];

static uint32_t seed = 12345;
static uint8_t rnd() {
  seed = seed * 1103515245u + 12345u;
  return seed >> 16;
}

static void boot() {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new mhavoc();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// random registers, zero page and object tables (0800-09ff), the VRAM
// pointer at $01 inside VRAM, a return address on the stack
static void randomize(uint16_t entry) {
  m6502_t &c = machine->m_cpu;
  for (int i = 0; i < 0x200; i++) {
    memory[i] = rnd();
    memory[mhavoc::RAM_HIGH_OFS + i] = rnd();
  }
  for (int i = 0; i < 0x1000; i++) {
    memory[dvgMachine::VRAM_OFS + i] = rnd();
  }
  const uint16_t ptr = 0x4000 + ((rnd() << 4 | rnd() >> 4) & 0x0ffe);
  memory[1] = ptr;
  memory[2] = ptr >> 8;
  c.a = rnd();
  c.x = rnd();
  c.y = rnd();
  c.p = (rnd() | M6502_FU) & ~(M6502_FB | M6502_FD);
  c.sp = 0x40 + (rnd() & 0x7f);
  c.irq = c.nmi = 0;
  c.pc = entry;
}

// x_end: object routines run natively only for X below it
static void check(uint16_t entry, int trials, uint16_t x_end = 0x100) {
  boot();
  m6502_t &c = machine->m_cpu;
  for (int t = 0; t < trials; t++) {
    randomize(entry);
    c.x %= x_end;
    const uint16_t ret = 0x8000 + ((rnd() << 8 | rnd()) & 0x7fff);
    memory[0x100 | uint8_t(c.sp + 1)] = (ret - 1) & 0xff;
    memory[0x100 | uint8_t(c.sp + 2)] = (ret - 1) >> 8;
    const uint8_t sp = c.sp;
    memcpy(saved, memory, RAMSIZE);
    const m6502_t start = c;

    uint32_t expect = 0;
    for (int i = 0; i < 100000 && !(c.pc == ret && c.sp == uint8_t(sp + 2)); i++) {
      expect += m6502_step(&c);
    }
    const m6502_t after = c;
    memcpy(interpreted, memory, RAMSIZE);

    memcpy(memory, saved, RAMSIZE);
    c = start;
    const uint32_t n = machine->native_alpha();
    char msg[96];
    snprintf(msg, sizeof(msg), "entry %04x trial %d a %02x x %02x y %02x p %02x", entry, t, start.a,
             start.x, start.y, start.p);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expect, n, msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(after.a, c.a, msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(after.x, c.x, msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(after.y, c.y, msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(after.p, c.p, msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(after.sp, c.sp, msg);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(after.pc, c.pc, msg);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(interpreted, memory, RAMSIZE, msg);
  }
}

static void test_vram_word(void) {
  check(0x995d, 5000);
  check(0x9959, 1000);
  check(0x9970, 1000);
}
static void test_vector(void) {
  check(0x9979, 5000);
  check(0x997b, 5000);
  check(0x997d, 5000);
  check(0x9998, 5000);
}
static void test_vram_advance(void) { check(0x9965, 5000); }
static void test_shift_right(void) { check(0xc6ec, 5000); }
static void test_multiply(void) { check(0x949f, 20000); }
static void test_scale(void) {
  check(0xb434, 20000, OBJECT_X_END);
  check(0xb448, 20000);
}
static void test_bounds(void) { check(0xb628, 20000, OBJECT_X_END); }
static void test_draw_object(void) { check(0xb655, 20000, OBJECT_X_END); }

// not run natively with an IRQ due or D set
static void test_irq_or_decimal_steps(void) {
  boot();
  m6502_t &c = machine->m_cpu;
  randomize(0x949f);
  c.irq = 1;
  c.p &= ~M6502_FI;
  TEST_ASSERT_EQUAL_UINT32(0, machine->native_alpha());
  randomize(0x949f);
  c.p |= M6502_FD;
  TEST_ASSERT_EQUAL_UINT32(0, machine->native_alpha());
  randomize(0x9961);
  TEST_ASSERT_EQUAL_UINT32(0, machine->native_alpha());
  randomize(0xb655);
  c.x = OBJECT_X_END;
  TEST_ASSERT_EQUAL_UINT32(0, machine->native_alpha());
}

// skip_alpha only looks for native routines and idle loops on trap pages
static void test_trap_pages(void) {
  boot();
  m6502_t &c = machine->m_cpu;
  for (uint32_t pc = 0; pc < 0x10000; pc++) {
    c.pc = pc;
    c.p = M6502_FU;
    c.x = 0;
    c.irq = c.nmi = 0;
    if (machine->native_alpha()) {
      TEST_ASSERT_TRUE_MESSAGE(trap_page(pc), "native routine off the trap pages");
    }
  }
  TEST_ASSERT_TRUE(trap_page(ALPHA_WAIT_PC));
  TEST_ASSERT_TRUE(trap_page(ALPHA_DELAY_PC));
  TEST_ASSERT_TRUE(trap_page(ALPHA_REPLY_PC));
  TEST_ASSERT_FALSE(trap_page(0x8000));
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_vram_word);
  RUN_TEST(test_vram_advance);
  RUN_TEST(test_vector);
  RUN_TEST(test_shift_right);
  RUN_TEST(test_multiply);
  RUN_TEST(test_scale);
  RUN_TEST(test_bounds);
  RUN_TEST(test_draw_object);
  RUN_TEST(test_irq_or_decimal_steps);
  RUN_TEST(test_trap_pages);
#endif
  return UNITY_END();
}
