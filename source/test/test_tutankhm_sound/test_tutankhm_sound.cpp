#include <stdio.h>
#include <string.h>
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
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }

static tutankhm *machine;
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return 0xff; }
void OutZ80(unsigned short p, unsigned char v) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }

static const uint16_t SOUND_LATCH = 0x8700;
static const uint16_t SOUND_TRIGGER = 0x8600;
static const int CHUNKS_PER_FRAME = 6;
static const int AY1_VOLUME_A = 8;

// Counts how often AY1 volume A changes between the register states the
// audio side renders. Sound 74 (Appear) drives that volume about once per
// millisecond as a software waveform (MAME: ~1300 writes in 0.8 s); one
// register snapshot per audio chunk sees at most 6 changes per frame.
static int volume_changes(int cmd, int frames) {
  Arena::init();
  machine = new tutankhm();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();

  int changes = 0;
  int last = 0;
  int16_t out[AUDIO_CHUNK];
  for (int f = 0; f < frames; f++) {
    if (f == 20) {
      machine->m6809_write(&machine->main_cpu, SOUND_LATCH, cmd);
      machine->m6809_write(&machine->main_cpu, SOUND_TRIGGER, 0);
      machine->m6809_write(&machine->main_cpu, SOUND_TRIGGER, 1);
    }
    machine->run_frame();
    for (int c = 0; c < CHUNKS_PER_FRAME; c++) {
      machine->aylog.play(out, AUDIO_CHUNK, [&](const uint8_t *regs, int16_t *, int) {
        changes += regs[AY1_VOLUME_A] != last;
        last = regs[AY1_VOLUME_A];
      });
    }
  }
  delete machine;
  return changes;
}

static void test_appear_volume_waveform(void) {
  TEST_ASSERT_GREATER_THAN(400, volume_changes(74, 70));
}
#endif

int main(void) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_appear_volume_waveform);
#endif
  return UNITY_END();
}
