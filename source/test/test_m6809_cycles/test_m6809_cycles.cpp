// m6809_step() cycle accounting when one call runs several instructions:
// machines that pace the 6809 by cycles (gng) read total_cycles.
#include <string.h>
#include <unity.h>

#include "../../src/cpus/m6809/m6809.c"

static uint8_t mem[0x10000];

uint8_t m6809_read(m6809_state *, uint16_t a) { return mem[a]; }
void m6809_write(m6809_state *, uint16_t a, uint8_t v) { mem[a] = v; }
uint8_t m6809_read_opcode(m6809_state *, uint16_t a) { return mem[a]; }

static m6809_state cpu;

void setUp(void) {
  memset(mem, 0, sizeof(mem));
  mem[0xfffe] = 0x10; // reset vector $1000
  mem[0xffff] = 0x00;
  m6809_reset(&cpu);
}
void tearDown(void) {}

// NOP (2 cycles), LDA ,X (indexed, no offset: 4 cycles)
static void test_indexed_after_other_op(void) {
  const uint8_t code[] = {0x12, 0xa6, 0x84};
  memcpy(mem + 0x1000, code, sizeof(code));
  cpu.total_cycles = 0;
  const int cycles = m6809_step(&cpu, 2);
  TEST_ASSERT_EQUAL_INT(6, cpu.total_cycles);
  TEST_ASSERT_EQUAL_INT(6, cycles);
}

// one instruction per call gives the same sum as several per call
static void test_count_independent(void) {
  // loop of the gng RAM test: CMPA ,X; BNE; LEAX 1,X; CMPX #$1FFF; BLS
  const uint8_t code[] = {0xa1, 0x84, 0x26, 0x00, 0x30, 0x01, 0x8c, 0x1f, 0xff, 0x23, 0xf5};
  memcpy(mem + 0x1000, code, sizeof(code));
  cpu.total_cycles = 0;
  for (int i = 0; i < 20; i++) {
    m6809_step(&cpu, 1);
  }
  const int single = cpu.total_cycles;

  m6809_reset(&cpu);
  cpu.total_cycles = 0;
  for (int i = 0; i < 5; i++) {
    m6809_step(&cpu, 4);
  }
  TEST_ASSERT_EQUAL_INT(single, cpu.total_cycles);
  TEST_ASSERT_EQUAL_INT(4 * 19, single); // 4 loop passes, 19 cycles each
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_indexed_after_other_op);
  RUN_TEST(test_count_independent);
  return UNITY_END();
}
