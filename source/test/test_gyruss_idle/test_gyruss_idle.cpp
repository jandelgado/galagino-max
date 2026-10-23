#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots Gyruss on the host through the machine class (main Z80, sub 6809,
// 8039 drums; the audio Z80 task is not started) and checks that the main
// CPU's task queue wait loop is skipped, not stepped. ROM headers come
// from romconv (./convert.sh gyruss); without them the test is skipped.
#if __has_include("../../src/machines/gyruss/gyruss_rom_main.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/i8048/i8048.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/gyruss/gyruss.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static gyruss *machine;
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }

// main CPU wait loop entry (gyruss.cpp MAIN_IDLE_PC)
static const uint16_t IDLE_PC = 0x05F4;
static uint32_t idleFetches, mainFetches;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) {
  idleFetches += a == IDLE_PC;
  mainFetches++;
  return machine->opZ80(a);
}
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }
void i8048_port_write(i8048_state_S *s, unsigned char p, unsigned char v) { machine->wrI8048_port(s, p, v); }
unsigned char i8048_port_read(i8048_state_S *s, unsigned char p) { return machine->rdI8048_port(s, p); }
unsigned char i8048_xdm_read(i8048_state_S *s, unsigned char a) { return machine->rdI8048_xdm(s, a); }
void i8048_xdm_write(i8048_state_S *, unsigned char, unsigned char) {}
unsigned char i8048_rom_read(i8048_state_S *s, unsigned short a) { return machine->rdI8048_rom(s, a); }

static const uint32_t ATTRACT_FRAMES = 1800, FRAMES = 600;

static uint32_t nonZero(const uint8_t *p, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += p[i] != 0;
  }
  return c;
}

// the loop ran ~225 passes per frame when stepped; skipped, it is entered a
// few times per frame (once per wait). Attract still draws: video RAM set.
static void test_idle_loop_skipped(void) {
  Arena::init();
  machine = new gyruss();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  for (uint32_t f = 0; f < ATTRACT_FRAMES; f++) {
    machine->run_frame();
  }
  idleFetches = mainFetches = 0;
  for (uint32_t f = 0; f < FRAMES; f++) {
    machine->run_frame();
  }
  const uint32_t vram = nonZero(&memory[GYR_VRAM_OFF], 0x400);
  printf("idle fetches per frame %u, main instructions per frame %u, vram %u\n", idleFetches / FRAMES,
         mainFetches / FRAMES, vram);
  TEST_ASSERT_LESS_THAN_UINT32(10 * FRAMES, idleFetches);
  TEST_ASSERT_GREATER_THAN_UINT32(100, vram);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_idle_loop_skipped);
#endif
  return UNITY_END();
}
