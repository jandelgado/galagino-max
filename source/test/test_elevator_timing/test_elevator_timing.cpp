#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Elevator Action CPU time base: both Z80s run on cycle budgets, so a
// DAC playback loop keeps its pitch (main 4 MHz, sound 3 MHz, 60 Hz).
// Test programs replace the ROMs; ROM headers are still needed to build.
#if __has_include("../../src/machines/elevator/elevator_main_rom.h")
#define HAVE_ROM 1
#define ENABLE_ELEVATOR
#define ELEVATOR_Z80_TABLES_INCLUDED
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/elevator/elevator.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }

static elevator *machine;
static uint32_t mainWrites, dacWrites;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) {
  if (machine->current_cpu == elevator::MAIN_CPU && a == 0x8000) {
    mainWrites++;
  }
  if (machine->current_cpu == elevator::SOUND_CPU && a == 0x4801) {
    dacWrites++;
  }
  machine->wrZ80(a, v);
}
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

// di / ld hl,addr / [ld (hl),a (7) / inc a (4) / jp loop (10)]: one write
// per 21 cycles
static void writeLoop(uint8_t *p, uint16_t addr) {
  const uint8_t prog[] = {0xf3, 0x21, (uint8_t)addr, (uint8_t)(addr >> 8),
                          0x77, 0x3c, 0xc3, 0x04, 0x00};
  memcpy(p, prog, sizeof(prog));
}

static void test_cpus_run_at_board_clocks(void) {
  static uint8_t mainProg[0x9000], soundProg[0x2000];
  Arena::init();
  machine = new elevator();
  machine->init(&input, frame_buffer, sprites, memory);
  writeLoop(mainProg, 0x8000);
  writeLoop(soundProg, 0x4801);   // AY#1 data: register 0 at reset
  machine->main_rom = mainProg;
  machine->sound_rom = soundProg;
  machine->reset();

  const uint32_t frames = 60;
  for (uint32_t f = 0; f < frames; f++) {
    machine->run_frame();
  }

  const uint32_t mainExpect = 4000000 / 21, soundExpect = 3000000 / 21;
  printf("writes per second: main %u (expect %u), sound %u (expect %u)\n", mainWrites,
         mainExpect, dacWrites, soundExpect);
  TEST_ASSERT_UINT32_WITHIN(mainExpect / 50, mainExpect, mainWrites);
  TEST_ASSERT_UINT32_WITHIN(soundExpect / 50, soundExpect, dacWrites);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_cpus_run_at_board_clocks);
#endif
  return UNITY_END();
}
