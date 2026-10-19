#include <string.h>
#include <unity.h>
#include "../../src/cpus/m6803/m6803.c"

// 6808 sound CPU of the Williams boards: plain 6800 timing, no 6803
// internal registers. Cycle counts from MAME m6800.cpp cycles_6800.
static uint8_t mem[0x10000];
static uint8_t rd(uint16_t a) { return mem[a]; }
static void wr(uint16_t a, uint8_t v) { mem[a] = v; }

static const uint16_t CODE = 0xf000;

static void boot(m6803_state *s, const uint8_t *code, size_t n) {
  memset(mem, 0, sizeof(mem));
  memcpy(mem + CODE, code, n);
  mem[0xfffe] = CODE >> 8;
  mem[0xffff] = CODE & 0xff;
  mem[0xfff8] = 0xf1;  // IRQ vector 0xf100
  mem[0xfff9] = 0x00;
  m6803_ext_read_fn = rd;
  m6803_ext_write_fn = wr;
  memset(s, 0, sizeof(*s));
  m6800_reset(s);
  s->SP = 0x00ff;
}

void setUp(void) {}
void tearDown(void) {}

void test_cycles_follow_6800_table(void) {
  // BSR +0 (6800 8, 6803 6), CPX #0 (3 vs 4), JSR ext (9), RTS (5)
  const uint8_t code[] = { 0x8d, 0x00, 0x8c, 0x00, 0x00, 0xbd, 0xf0, 0x09, 0x01, 0x39 };
  m6803_state s;
  boot(&s, code, sizeof(code));
  TEST_ASSERT_EQUAL(8, m6800_step(&s));
  TEST_ASSERT_EQUAL(3, m6800_step(&s));
  TEST_ASSERT_EQUAL(9, m6800_step(&s));
  TEST_ASSERT_EQUAL(5, m6800_step(&s));
}

void test_zero_page_is_external(void) {
  // LDAA #$5a, STAA $08 (6803 TCSR), STAA $90 (6803 internal RAM)
  const uint8_t code[] = { 0x86, 0x5a, 0x97, 0x08, 0x97, 0x90 };
  m6803_state s;
  boot(&s, code, sizeof(code));
  for (int i = 0; i < 3; i++) {
    m6800_step(&s);
  }
  TEST_ASSERT_EQUAL_HEX8(0x5a, mem[0x08]);
  TEST_ASSERT_EQUAL_HEX8(0x5a, mem[0x90]);
}

void test_irq_entry(void) {
  const uint8_t code[] = { 0x0e, 0x01 };  // CLI, NOP
  m6803_state s;
  boot(&s, code, sizeof(code));
  TEST_ASSERT_EQUAL(2, m6800_step(&s));
  m6803_irq(&s);
  TEST_ASSERT_EQUAL(12, m6800_step(&s));
  TEST_ASSERT_EQUAL_HEX16(0xf100, s.PC);
}

void test_6803_reset_keeps_6803_mode(void) {
  m6803_state s;
  memset(&s, 0xff, sizeof(s));
  m6803_reset(&s);
  TEST_ASSERT_EQUAL(0, s.mode6800);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_cycles_follow_6800_table);
  RUN_TEST(test_zero_page_is_external);
  RUN_TEST(test_irq_entry);
  RUN_TEST(test_6803_reset_keeps_6803_mode);
  return UNITY_END();
}
