#include <string.h>
#include <unity.h>

// i8048 core: per-cycle counter and MOVX write forwarding (mario DAC).
#include "../../src/cpus/i8048/i8048.c"

static unsigned char rom[16];
static unsigned char xdm_addr, xdm_data;
static int xdm_writes;

extern "C" {
void i8048_port_write(i8048_state_S *, unsigned char, unsigned char) {}
unsigned char i8048_port_read(i8048_state_S *, unsigned char) { return 0; }
unsigned char i8048_xdm_read(i8048_state_S *, unsigned char) { return 0; }
void i8048_xdm_write(i8048_state_S *, unsigned char addr, unsigned char data) {
  xdm_addr = addr;
  xdm_data = data;
  xdm_writes++;
}
unsigned char i8048_rom_read(i8048_state_S *, unsigned short addr) { return rom[addr & 15]; }
}

void setUp(void) {}
void tearDown(void) {}

static void test_cycles_and_movx_write(void) {
  // MOV R0,#0x5a; MOV A,#0x33; NOP; MOVX @R0,A; JMP 0
  static const unsigned char prog[] = { 0xb8, 0x5a, 0x23, 0x33, 0x00, 0x90, 0x04, 0x00 };
  memcpy(rom, prog, sizeof(prog));
  i8048_state_S s;
  i8048_reset(&s);
  TEST_ASSERT_EQUAL_UINT32(0, s.cycles);
  i8048_step(&s);
  i8048_step(&s);
  TEST_ASSERT_EQUAL_UINT32(2 + 2, s.cycles);
  i8048_step(&s);
  TEST_ASSERT_EQUAL_UINT32(2 + 2 + 1, s.cycles);
  i8048_step(&s);
  TEST_ASSERT_EQUAL_UINT32(2 + 2 + 1 + 2, s.cycles);
  TEST_ASSERT_EQUAL_INT(1, xdm_writes);
  TEST_ASSERT_EQUAL_HEX8(0x5a, xdm_addr);
  TEST_ASSERT_EQUAL_HEX8(0x33, xdm_data);
  i8048_step(&s);
  TEST_ASSERT_EQUAL_UINT32(2 + 2 + 1 + 2 + 2, s.cycles);
  TEST_ASSERT_EQUAL_UINT16(0, s.PC);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_cycles_and_movx_write);
  return UNITY_END();
}
