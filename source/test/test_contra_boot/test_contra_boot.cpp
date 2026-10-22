#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots the Contra ROM on the host through the machine class: HD6309 main CPU
// run as a plain 6809, IRQ from K007121 #0, banked ROM, 007452 math. ROM
// headers come from romconv (./convert.sh contra); without them the test is
// skipped.
#if __has_include("../../src/machines/contra/contra_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_CONTRA
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/sid.cpp"
#include "../../src/emulation/sidplayer.cpp"
#define private public
#define protected public
#include "../../src/machines/contra/contra.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

extern "C" void ResetZ80(Z80 *) {}
void Reset6502(M6502 *) {}

static Input input;
static unsigned short frame_buffer[224 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static contra *machine;
static uint32_t frame;

// 6809 page 0 opcodes without a 6809 meaning (HD6309 native or illegal)
static const uint8_t ILLEGAL_P0[] = {0x01, 0x02, 0x05, 0x0b, 0x14, 0x15, 0x18, 0x1b, 0x38, 0x41, 0x42,
                                     0x45, 0x4b, 0x4e, 0x51, 0x52, 0x55, 0x5b, 0x5e, 0x61, 0x62, 0x65,
                                     0x6b, 0x71, 0x72, 0x75, 0x7b, 0x87, 0x8f, 0xc7, 0xcd, 0xcf};
// valid second bytes after prefix 0x10 / 0x11
static const uint8_t VALID_P2[] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
                                   0x2c, 0x2d, 0x2e, 0x2f, 0x3f, 0x83, 0x8c, 0x8e, 0x93, 0x9c, 0x9e,
                                   0x9f, 0xa3, 0xac, 0xae, 0xaf, 0xb3, 0xbc, 0xbe, 0xbf, 0xce, 0xde,
                                   0xdf, 0xee, 0xef, 0xfe, 0xff};
static const uint8_t VALID_P3[] = {0x3f, 0x83, 0x8c, 0x93, 0x9c, 0xa3, 0xac, 0xb3, 0xbc};
static uint32_t unknownOps, opFetches;

static bool in(const uint8_t *set, size_t n, uint8_t v) {
  for (size_t i = 0; i < n; i++) {
    if (set[i] == v) {
      return true;
    }
  }
  return false;
}

static uint32_t irqs;   // IRQ vector fetches
unsigned char m6809_read(m6809_state *s, uint16_t a) {
  irqs += a == 0xfff8;
  return machine->m6809_read(s, a);
}
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) {
  const uint8_t op = machine->m6809_read_opcode(s, a);
  const uint8_t next = machine->m6809_read(s, (uint16_t)(a + 1));
  opFetches++;
  if (in(ILLEGAL_P0, sizeof(ILLEGAL_P0), op) ||
      (op == 0x10 && !in(VALID_P2, sizeof(VALID_P2), next)) ||
      (op == 0x11 && !in(VALID_P3, sizeof(VALID_P3), next))) {
    if (unknownOps < 10) {
      printf("unknown opcode %02x %02x at %04x\n", op, next, a);
    }
    unknownOps++;
  }
  return op;
}

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new contra();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// all opcode fetches through m6809_read_opcode, for the opcode check
static void runTo(uint32_t end) {
  while (frame < end) {
    machine->main_cpu.rom_size = 0;
    machine->run_frame();
    frame++;
  }
}

static uint32_t differing(const uint8_t *p, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += p[i] != p[0];
  }
  return c;
}

// attract demo (MAME: stage 1 from ~16 s): both scroll layers and the HUD filled
static void test_boots_to_attract(void) {
  boot();
  runTo(1500);
  const uint32_t l0 = differing(&machine->ram[CONTRA_VRAM0], 0x400);
  const uint32_t l1 = differing(&machine->ram[CONTRA_VRAM1], 0x400);
  const uint32_t hud = differing(&machine->ram[CONTRA_VRAM2], 0x400);
  printf("layer0 %u, layer1 %u, hud %u, IRQs %u\n", l0, l1, hud, irqs);
  TEST_ASSERT_GREATER_THAN_UINT32(50, l0);
  TEST_ASSERT_GREATER_THAN_UINT32(50, l1);
  TEST_ASSERT_GREATER_THAN_UINT32(20, hud);
  // MAME 0.289: 1101 IRQ vector reads in frames 2-1500 (boot tests run without IRQ)
  TEST_ASSERT_UINT32_WITHIN(5, 1101, irqs);
}

// the bootleg PCB runs this ROM on a 68B09: no HD6309-only opcode may run
static void test_no_unknown_opcodes(void) {
  boot();
  runTo(3600);
  printf("opcode fetches %u, unknown %u\n", opFetches, unknownOps);
  TEST_ASSERT_GREATER_THAN_UINT32(1000000, opFetches);
  TEST_ASSERT_EQUAL_UINT32(0, unknownOps);
}

// 007452: 0x12 * 0x34 and 1000 / 7 (k007452.cpp write/read)
static void test_math_chip(void) {
  boot();
  machine->m6809_write(&machine->main_cpu, 0x08, 0x12);
  machine->m6809_write(&machine->main_cpu, 0x09, 0x34);
  TEST_ASSERT_EQUAL_UINT16(0x12 * 0x34, machine->m6809_read(&machine->main_cpu, 0x08) |
                                            machine->m6809_read(&machine->main_cpu, 0x09) << 8);

  machine->m6809_write(&machine->main_cpu, 0x0a, 0);            // divisor
  machine->m6809_write(&machine->main_cpu, 0x0b, 7);
  machine->m6809_write(&machine->main_cpu, 0x0c, 1000 >> 8);    // dividend, 0x0d starts
  machine->m6809_write(&machine->main_cpu, 0x0d, 1000 & 0xff);
  TEST_ASSERT_EQUAL_UINT8(1000 / 7, machine->m6809_read(&machine->main_cpu, 0x0c));
  TEST_ASSERT_EQUAL_UINT8(1000 % 7, machine->m6809_read(&machine->main_cpu, 0x0a));
}

static void test_dip_switches(void) {
  boot();
  TEST_ASSERT_EQUAL_HEX8(CONTRA_DSW1, machine->m6809_read(&machine->main_cpu, 0x14));
  TEST_ASSERT_EQUAL_HEX8(CONTRA_DSW2, machine->m6809_read(&machine->main_cpu, 0x15));
  TEST_ASSERT_EQUAL_HEX8(CONTRA_DSW3, machine->m6809_read(&machine->main_cpu, 0x16));
}

// P1 active low: fire on FIRE, jump (button 2) on JUMP_KEY (START / EXTRA)
static void test_inputs(void) {
  boot();
  keys = BUTTON_LEFT | BUTTON_FIRE;
  TEST_ASSERT_EQUAL_HEX8(0xff & ~0x01 & ~0x10, machine->m6809_read(&machine->main_cpu, 0x11));
  keys = JUMP_KEY;
  TEST_ASSERT_EQUAL_HEX8(0xff & ~0x20, machine->m6809_read(&machine->main_cpu, 0x11));
  keys = BUTTON_COIN;
  TEST_ASSERT_EQUAL_HEX8(0xff & ~0x01, machine->m6809_read(&machine->main_cpu, 0x10));
  keys = 0;
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_math_chip);
  RUN_TEST(test_dip_switches);
  RUN_TEST(test_boots_to_attract);
  RUN_TEST(test_no_unknown_opcodes);
  RUN_TEST(test_inputs);
#endif
  return UNITY_END();
}
