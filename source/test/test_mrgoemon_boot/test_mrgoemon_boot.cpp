#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots the Mr. Goemon ROM on the host through the mrgoemon machine class
// (gberet board, banked ROM window f800-ffff). ROM headers come from romconv
// (./convert.sh mrgoemon); without them the test is skipped.
#if __has_include("../../src/machines/mrgoemon/mrgoemon_rom.h")
#define HAVE_ROM 1
#define GAME_MRGOEMON
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/gberet/gberet.cpp"
#include "../../src/machines/mrgoemon/mrgoemon.cpp"
#undef private
#undef protected
#include "mame_mrgoemon_vram.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }

static mrgoemon *machine;
static uint32_t frame;
static uint32_t bankOps, bankReads;  // opcode / data reads in f800-ffff
static uint32_t bootBankOps;         // bankOps after the self-test
static uint8_t banksUsed;            // banks selected after frame 150

extern "C" {
unsigned char OpZ80_INL(unsigned short a) {
  bankOps += a >= 0xf800;
  return machine->opZ80(a);
}
unsigned char OpPrefixZ80_INL(unsigned short a) {
  bankOps += a >= 0xf800;
  return machine->opPrefixZ80(a);
}
unsigned char RdZ80(unsigned short a) {
  bankReads += a >= 0xf800;
  return machine->rdZ80(a);
}
void WrZ80(unsigned short a, unsigned char v) {
  if (a == 0xf000 && frame >= 150) {
    banksUsed |= 1 << (v >> 5);
  }
  machine->wrZ80(a, v);
}
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  Arena::reserve(&mrgoemon_arena_plan);  // as main.cpp's switchTo()
  machine = new mrgoemon();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// PC outside ROM (0000-bfff, f800-ffff) and work RAM (d200-dfff) at a frame end
static uint32_t badPcFrames;

static void runTo(uint32_t f) {
  while (frame < f) {
    machine->run_frame();
    frame++;
    const uint16_t pc = machine->cpu[0].PC.W;
    badPcFrames += !(pc < 0xc000 || pc >= 0xf800 || (pc >= 0xd200 && pc < 0xe000));
  }
}

// boot ROM check runs from the bank window (MAME: PC in fa12-fa4d, frames 1-20)
static void test_boot_runs_banked_code(void) {
  boot();
  runTo(100);
  printf("f800-ffff opcode fetches %u, data reads %u\n", bankOps, bankReads);
  TEST_ASSERT_GREATER_THAN_UINT32(10000, bankOps);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, bankReads);
  bootBankOps = bankOps;
}

// Host self-test (instruction budget per frame, not Z80 cycles) ends 43
// frames before MAME's; from then on the game runs on the frame IRQ.
// MAME title frame 570 (blink on, frames 568-575) = host frame 527-534.
static void test_title_vram_matches_mame(void) {
  boot();
  runTo(530);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(mame_mrgoemon_vram_570, machine->memory + GB_MEM_CRAM, 0x1000);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(mame_mrgoemon_spr_570, machine->memory + GB_MEM_SPR2, 0x200);
}

static uint32_t differing(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += a[i] != b[i];
  }
  return c;
}

// attract gameplay demo, scrolled into the second screen. MAME frame 1800 =
// host 1764 (tiles equal in host 1759-1775). One sprite (d08c/d170) differs.
static void test_demo_vram_matches_mame(void) {
  boot();
  runTo(1764);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(mame_mrgoemon_vram_1800, machine->memory + GB_MEM_CRAM, 0x1000);
  const uint32_t d = differing(mame_mrgoemon_spr_1800, machine->memory + GB_MEM_SPR2, 0x200);
  printf("sprite RAM bytes unlike MAME: %u\n", d);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(6, d);
}

// one minute of attract: title, demo, no runaway CPU. MAME selects banks 0,
// 4 and 7 from frame 150 (self-test ends ~118); the demo runs banked code.
static void test_attract_runs(void) {
  boot();
  runTo(3600);
  printf("frames with PC outside ROM/RAM: %u, f800-ffff fetches %u\n", badPcFrames, bankOps);
  printf("banks used after frame 150: %02x\n", banksUsed);
  TEST_ASSERT_EQUAL_UINT32(0, badPcFrames);
  TEST_ASSERT_EQUAL_HEX8(0x91, banksUsed);
  TEST_ASSERT_GREATER_THAN_UINT32(bootBankOps + 1000, bankOps);
}

// f000 b5-7 select the 2 KB bank of ROM c000-ffff, reset selects bank 0
static void test_bank_window(void) {
  boot();
  const uint8_t *rom = machine->rom_ptr;
  machine->wrZ80(0xf000, 0xa3);  // bank 5, coin counters set
  TEST_ASSERT_EQUAL_HEX8(rom[0xc000 + 5 * 0x800], machine->rdZ80(0xf800));
  TEST_ASSERT_EQUAL_HEX8(rom[0xc000 + 5 * 0x800 + 0x7ff], machine->opZ80(0xffff));
  machine->wrZ80(0xf000, 0xe0);
  TEST_ASSERT_EQUAL_HEX8(rom[0xc000 + 7 * 0x800 + 0x123], machine->rdZ80(0xf923));
  machine->reset();
  TEST_ASSERT_EQUAL_HEX8(rom[0xc000 + 0x123], machine->opZ80(0xf923));
}

static void test_dsw2_default(void) {
  boot();
  TEST_ASSERT_EQUAL_HEX8(0x5a, machine->rdZ80(0xf200));
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boot_runs_banked_code);
  RUN_TEST(test_title_vram_matches_mame);
  RUN_TEST(test_demo_vram_matches_mame);
  RUN_TEST(test_attract_runs);
  RUN_TEST(test_bank_window);  // last: resets the machine
  RUN_TEST(test_dsw2_default);
#else
  printf("mrgoemon ROM headers missing, run romconv\n");
#endif
  return UNITY_END();
}
