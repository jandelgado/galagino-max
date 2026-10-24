#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unity.h>

#if __has_include("../../src/machines/tutankhm/tutankhm_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/tutankhm/tutankhm.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static tutankhm *machine;
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static int frame;
static int aywr[2400];

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v);
unsigned char InZ80(unsigned short p) { return 0xff; }
void OutZ80(unsigned short p, unsigned char v) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}
extern "C" void WrZ80(unsigned short a, unsigned char v) {
  if ((a & 0xF000) == 0x4000 || (a & 0xF000) == 0x6000) {
    aywr[frame]++;
    const int c = (a & 0xF000) == 0x6000;
    if (getenv("VERBOSE")) printf("w f%d ay%d r%d=%d\n", frame, c, machine->ay_addr[c], v);
  }
  machine->wrZ80(a, v);
}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }

void m6809_write(m6809_state *s, uint16_t a, uint8_t v) {
  if ((a & 0xFF00) == 0x8700 || (a & 0xFF00) == 0x8600) {
    if (frame < 0) printf("f%d %s %d\n", frame, (a & 0xFF00) == 0x8700 ? "latch" : "trig", v);
  }
  machine->m6809_write(s, a, v);
}

static void test_trace(void) {
  const char *c = getenv("CMD");
  const int cmd = c ? atoi(c) : 77;
  Arena::init();
  machine = new tutankhm();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  int total = 0;
  for (frame = 0; frame < 170; frame++) {
    if (frame == 20) {
      printf("c f%d cmd %d\n", frame, cmd);
      machine->m6809_write(&machine->main_cpu, 0x8700, cmd);
      machine->m6809_write(&machine->main_cpu, 0x8600, 0);
      machine->m6809_write(&machine->main_cpu, 0x8600, 1);
    }
    if (frame == 21) {
      machine->m6809_write(&machine->main_cpu, 0x8600, 0);
    }
    machine->run_frame();
    if (frame >= 20) {
      total += aywr[frame];
    }
  }
  printf("total %d sp %04x pc %04x\n", total, machine->cpu[0].SP.W, machine->cpu[0].PC.W);
}
#endif

int main(void) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_trace);
#endif
  return UNITY_END();
}
