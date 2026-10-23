// i8086 core: what the galagino port changed in FabGL's interpreter (bus
// windows, NMI, prefix steps, faults, 8088 shifts, AF). The instruction set
// itself is covered by test_qbert_boot's lockstep against MAME.
#include <string.h>
#include <unity.h>

#include "../../src/cpus/i8086/i8086.cpp"

static const uint16_t CODE = 0x8000, STACK = 0x1ff0, RAM_SIZE = 0x3000, NMI_HANDLER = 0x8100;

static uint8_t mem[0x10000];

struct Access {
  bool write;
  uint32_t address;
  uint8_t value;
};
static Access accessLog[16];
static int accessCount;

static uint8_t busRead(void *, uint32_t a) {
  if (accessCount < 16) {
    accessLog[accessCount++] = {false, a, mem[a]};
  }
  return mem[a];
}

static void busWrite(void *, uint32_t a, uint8_t v) {
  if (accessCount < 16) {
    accessLog[accessCount++] = {true, a, v};
  }
  mem[a] = v;
}

static uint16_t word(uint16_t a) { return (uint16_t)(mem[a] | mem[a + 1] << 8); }

void setUp(void) {
  memset(mem, 0, sizeof(mem));
  accessCount = 0;
  const i8086::Bus bus = {nullptr, busRead, busWrite, mem, RAM_SIZE, mem + CODE, CODE, CODE, 0xffff};
  i8086::setBus(bus);
  i8086::reset();
  mem[0x0008] = NMI_HANDLER & 0xff;   // int 2 vector 0000:8100
  mem[0x0009] = NMI_HANDLER >> 8;
  mem[NMI_HANDLER] = 0xcf;            // iret
}
void tearDown(void) {}

static void load(const uint8_t *code, size_t n) {
  memcpy(mem + CODE, code, n);
  i8086::setCS(0);
  i8086::setDS(0);
  i8086::setES(0);
  i8086::setSS(0);
  i8086::setSP(STACK);
  i8086::setIP(CODE);
}

// runs code at 0x8000 until IP leaves it at the end; flags and registers
// carry over from the previous run
static void run(const uint8_t *code, size_t n) {
  load(code, n);
  int steps = 0;
  while (i8086::IP() != CODE + n && steps < 1000) {
    i8086::step();
    steps++;
  }
  TEST_ASSERT_EQUAL_HEX16(CODE + n, i8086::IP());
}

#define RUN(...)                                   \
  do {                                             \
    const uint8_t code_[] = {__VA_ARGS__};         \
    run(code_, sizeof(code_));                     \
  } while (0)

static void test_reset_runs_from_fff0(void) {
  const uint8_t jmp[] = {0xea, 0x00, 0x81, 0x00, 0x00};   // jmp 0000:8100
  memcpy(mem + 0xfff0, jmp, sizeof(jmp));
  i8086::reset();
  i8086::step();
  TEST_ASSERT_EQUAL_HEX16(0, i8086::CS());
  TEST_ASSERT_EQUAL_HEX16(0x8100, i8086::IP());
}

static void test_code_window(void) {
  static uint8_t image[0x10000 - 0x1000];   // code window 0x1000 .. 0xffff
  const uint8_t code[] = {0xa0, 0x00, 0x38};   // mov al,[3800h]
  memcpy(image, code, sizeof(code));
  mem[0x3800] = 0x5a;                          // only the read callback sees it
  const i8086::Bus bus = {nullptr, busRead, busWrite, mem, 0x1000, image, 0x1000, 0x6000, 0xffff};
  i8086::setBus(bus);
  i8086::setCS(0);
  i8086::setDS(0);
  i8086::setIP(0x1000);
  i8086::step();
  TEST_ASSERT_EQUAL_HEX8(0x5a, i8086::AL());
  TEST_ASSERT_FALSE(i8086::fault());
}

static void test_register_operands_skip_bus(void) {
  static uint8_t before[RAM_SIZE];
  memcpy(before, mem, RAM_SIZE);
  RUN(0xb8, 0x34, 0x12,   // mov ax,0x1234
      0x89, 0xc3,         // mov bx,ax
      0x88, 0xf9);        // mov cl,bh
  TEST_ASSERT_EQUAL_HEX16(0x1234, i8086::BX());
  TEST_ASSERT_EQUAL_HEX8(0x12, i8086::CL());
  TEST_ASSERT_EQUAL(0, accessCount);
  TEST_ASSERT_EQUAL_MEMORY(before, mem, RAM_SIZE);
}

static void test_bus_windows(void) {
  mem[0x9000] = 0x77;
  mem[0x5000] = 0x66;
  RUN(0xb0, 0x5a,         // mov al,0x5a
      0xa2, 0xff, 0x2f,   // mov [0x2fff],al   RAM window
      0xa2, 0x00, 0x30,   // mov [0x3000],al   callback
      0xa0, 0x00, 0x90,   // mov al,[0x9000]   ROM window
      0x88, 0xc3,         // mov bl,al
      0xa0, 0x00, 0x50);  // mov al,[0x5000]   callback
  TEST_ASSERT_EQUAL_HEX8(0x5a, mem[0x2fff]);
  TEST_ASSERT_EQUAL_HEX8(0x77, i8086::BL());
  TEST_ASSERT_EQUAL_HEX8(0x66, i8086::AL());
  TEST_ASSERT_EQUAL(2, accessCount);
  TEST_ASSERT_TRUE(accessLog[0].write);
  TEST_ASSERT_EQUAL_HEX32(0x3000, accessLog[0].address);
  TEST_ASSERT_EQUAL_HEX8(0x5a, accessLog[0].value);
  TEST_ASSERT_FALSE(accessLog[1].write);
  TEST_ASSERT_EQUAL_HEX32(0x5000, accessLog[1].address);
}

static void test_word_across_ram_end(void) {
  RUN(0xb8, 0xef, 0xbe,   // mov ax,0xbeef
      0xa3, 0xff, 0x2f);  // mov [0x2fff],ax
  TEST_ASSERT_EQUAL_HEX8(0xef, mem[0x2fff]);
  TEST_ASSERT_EQUAL(1, accessCount);
  TEST_ASSERT_TRUE(accessLog[0].write);
  TEST_ASSERT_EQUAL_HEX32(0x3000, accessLog[0].address);
  TEST_ASSERT_EQUAL_HEX8(0xbe, accessLog[0].value);
}

static void test_nmi_frame(void) {
  RUN(0x31, 0xc0);        // xor ax,ax
  i8086::nmi();
  TEST_ASSERT_EQUAL_HEX16(0, i8086::CS());
  TEST_ASSERT_EQUAL_HEX16(NMI_HANDLER, i8086::IP());
  TEST_ASSERT_EQUAL_HEX16(STACK - 6, i8086::SP());
  TEST_ASSERT_EQUAL_HEX16(0x8002, word(STACK - 6));
  TEST_ASSERT_EQUAL_HEX16(0, word(STACK - 4));
  TEST_ASSERT_EQUAL_HEX16(0xf046, word(STACK - 2));
  i8086::step();          // iret
  TEST_ASSERT_EQUAL_HEX16(0x8002, i8086::IP());
  TEST_ASSERT_TRUE(i8086::flagZF());
  TEST_ASSERT_EQUAL_HEX16(STACK, i8086::SP());
}

// the rep and its prefix are one step: an NMI can only come after it
static void test_nmi_after_rep(void) {
  const uint8_t code[] = {0xfc,               // cld
                          0xb9, 0x04, 0x00,   // mov cx,4
                          0xbf, 0x00, 0x01,   // mov di,0x100
                          0xb8, 0x34, 0x12,   // mov ax,0x1234
                          0xf3, 0xab};        // rep stosw
  const uint16_t REP = CODE + 10;
  load(code, sizeof(code));
  for (int i = 0; i < 10 && i8086::IP() != REP; i++) {
    i8086::step();
  }
  TEST_ASSERT_EQUAL_HEX16(REP, i8086::IP());
  i8086::step();
  TEST_ASSERT_EQUAL_HEX16(CODE + sizeof(code), i8086::IP());
  TEST_ASSERT_EQUAL_HEX16(0, i8086::CX());
  TEST_ASSERT_EQUAL_HEX16(0x108, i8086::DI());
  for (uint16_t a = 0x100; a < 0x108; a += 2) {
    TEST_ASSERT_EQUAL_HEX16(0x1234, word(a));
  }
  i8086::nmi();
  i8086::step();          // iret
  TEST_ASSERT_EQUAL_HEX16(CODE + sizeof(code), i8086::IP());
}

static void test_fetch_outside_rom_faults(void) {
  const uint8_t code[] = {0xb8, 0x00, 0x40,   // mov ax,0x4000
                          0xff, 0xe0};        // jmp ax
  load(code, sizeof(code));
  i8086::step();
  i8086::step();
  TEST_ASSERT_FALSE(i8086::fault());
  i8086::step();
  TEST_ASSERT_TRUE(i8086::fault());
  TEST_ASSERT_EQUAL_HEX16(0x4000, i8086::IP());
  i8086::reset();
  TEST_ASSERT_FALSE(i8086::fault());
}

// the 8088 does not mask the shift count (the 80186 masks it to 5 bits)
static void test_shift_count_not_masked(void) {
  RUN(0xb8, 0x01, 0x00, 0xb1, 0x10, 0xd3, 0xe0);   // mov ax,1; mov cl,16; shl ax,cl
  TEST_ASSERT_EQUAL_HEX16(0, i8086::AX());
  TEST_ASSERT_TRUE(i8086::flagCF());
  TEST_ASSERT_TRUE(i8086::flagZF());
  RUN(0xb8, 0x01, 0x00, 0xb1, 0x21, 0xd3, 0xe0);   // mov ax,1; mov cl,33; shl ax,cl (80186: 2)
  TEST_ASSERT_EQUAL_HEX16(0, i8086::AX());
  TEST_ASSERT_FALSE(i8086::flagCF());
}

// AF after and/or/xor: 0, as MAME's i86 (undefined on the chip)
static void test_logic_clears_af(void) {
  RUN(0xb0, 0x0f, 0x04, 0x01);   // mov al,0x0f; add al,1: AF
  TEST_ASSERT_TRUE(i8086::flagAF());
  RUN(0xb0, 0x03, 0x24, 0x03);   // mov al,3; and al,3
  TEST_ASSERT_FALSE(i8086::flagAF());
}

static void test_unsupported_opcode_faults(void) {
  const uint8_t code[] = {0xd8, 0xc0};   // 8087 escape
  load(code, sizeof(code));
  i8086::step();
  TEST_ASSERT_TRUE(i8086::fault());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_reset_runs_from_fff0);
  RUN_TEST(test_code_window);
  RUN_TEST(test_register_operands_skip_bus);
  RUN_TEST(test_bus_windows);
  RUN_TEST(test_word_across_ram_end);
  RUN_TEST(test_nmi_frame);
  RUN_TEST(test_nmi_after_rep);
  RUN_TEST(test_fetch_outside_rom_faults);
  RUN_TEST(test_shift_count_not_masked);
  RUN_TEST(test_logic_clears_af);
  RUN_TEST(test_unsupported_opcode_faults);
  return UNITY_END();
}
