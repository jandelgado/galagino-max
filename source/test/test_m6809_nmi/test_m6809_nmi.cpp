// m6809_nmi() must vector to the NMI handler like m6809_irq() does for IRQ
// (yiear: music tempo NMI every 32 lines).
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
  mem[0xfffc] = 0x30; // NMI vector $3000
  // LDS #$2000 (arms NMI), BRA *
  const uint8_t code[] = {0x10, 0xce, 0x20, 0x00, 0x20, 0xfe};
  memcpy(mem + 0x1000, code, sizeof(code));
  mem[0x3000] = 0x12; // NOP
  m6809_reset(&cpu);
  m6809_step(&cpu, 2);
}
void tearDown(void) {}

static void test_nmi_vectors(void) {
  m6809_nmi(&cpu);
  m6809_step(&cpu, 1);
  TEST_ASSERT_EQUAL_HEX16(0x3001, cpu.PC);
  TEST_ASSERT_EQUAL_HEX16(0x2000 - 12, cpu.S); // entire state pushed
}

// before LDS the NMI stays pending, taken once armed
static void test_nmi_waits_for_lds(void) {
  m6809_reset(&cpu);
  m6809_nmi(&cpu);
  TEST_ASSERT_EQUAL_HEX16(0x1000, cpu.PC);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_nmi_vectors);
  RUN_TEST(test_nmi_waits_for_lds);
  return UNITY_END();
}
