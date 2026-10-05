// Encrypted-opcode CPUs (Commando) decrypt every M1 fetch. The byte after a
// CB/ED/DD/FD prefix is an M1 fetch too, operands and the DDCB/FDCB
// displacement + opcode are not.
#include <string.h>
#include <unity.h>

#include "../../src/cpus/z80/Z80.c"

static uint8_t op[0x100];   // M1 view (decrypted opcodes)
static uint8_t data[0x100]; // plain view (operands)

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return op[a & 0xff]; }
unsigned char OpPrefixZ80_INL(unsigned short a) { return op[a & 0xff]; }
unsigned char RdZ80(unsigned short a) { return data[a & 0xff]; }
void WrZ80(unsigned short a, unsigned char v) { data[a & 0xff] = v; }
unsigned char InZ80(unsigned short) { return 0xff; }
void OutZ80(unsigned short, unsigned char) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static Z80 cpu;

void setUp(void) {
  memset(op, 0, sizeof(op));
  memset(data, 0, sizeof(data));
  memset(&cpu, 0, sizeof(cpu));
}
void tearDown(void) {}

static void test_dd_second_byte_from_m1(void) {
  const uint8_t m1[] = {0xdd, 0x21};               // LD IX,nn
  const uint8_t plain[] = {0x00, 0x00, 0x34, 0x12}; // operands only
  memcpy(op, m1, sizeof(m1));
  memcpy(data, plain, sizeof(plain));
  StepZ80(&cpu);
  TEST_ASSERT_EQUAL_HEX16(0x1234, cpu.IX.W);
  TEST_ASSERT_EQUAL_HEX16(4, cpu.PC.W);
}

static void test_fd_second_byte_from_m1(void) {
  const uint8_t m1[] = {0xfd, 0x21};               // LD IY,nn
  const uint8_t plain[] = {0x00, 0x00, 0x78, 0x56};
  memcpy(op, m1, sizeof(m1));
  memcpy(data, plain, sizeof(plain));
  StepZ80(&cpu);
  TEST_ASSERT_EQUAL_HEX16(0x5678, cpu.IY.W);
}

static void test_ed_second_byte_from_m1(void) {
  op[0] = 0xed;
  op[1] = 0x47; // LD I,A
  cpu.AF.B.h = 0x5a;
  StepZ80(&cpu);
  TEST_ASSERT_EQUAL_HEX8(0x5a, cpu.I);
}

static void test_cb_second_byte_from_m1(void) {
  op[0] = 0xcb;
  op[1] = 0x07; // RLC A
  cpu.AF.B.h = 0x81;
  StepZ80(&cpu);
  TEST_ASSERT_EQUAL_HEX8(0x03, cpu.AF.B.h);
}

// DD CB d op: d and op are memory reads, not M1
static void test_ddcb_displacement_and_opcode_plain(void) {
  op[0] = 0xdd;
  op[1] = 0xcb;
  data[2] = 0x05; // d
  data[3] = 0x06; // RLC (IX+d)
  data[0x15] = 0x81;
  cpu.IX.W = 0x10;
  StepZ80(&cpu);
  TEST_ASSERT_EQUAL_HEX8(0x03, data[0x15]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_dd_second_byte_from_m1);
  RUN_TEST(test_fd_second_byte_from_m1);
  RUN_TEST(test_ed_second_byte_from_m1);
  RUN_TEST(test_cb_second_byte_from_m1);
  RUN_TEST(test_ddcb_displacement_and_opcode_plain);
  return UNITY_END();
}
