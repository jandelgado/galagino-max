#include <stdio.h>
#include <stdlib.h>
#include <unity.h>

#if __has_include("../../src/machines/supercobra/supercobra_main_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#include "../../src/machines/tileaddr.cpp"
#define private public
#define protected public
#include "../../src/machines/scramble/scramble.cpp"
#include "../../src/machines/supercobra/supercobra.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }

static machineBase *machine;
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opPrefixZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}

static const uint16_t SOUND_LATCH = 0xa000;
static const uint16_t SOUND_TRIGGER = 0xa001;
static const uint8_t TRIGGER_IRQ = 0x08;
static const int CHUNKS_PER_FRAME = 6;
static const int AY_REGS = 28;

// Register changes between the states the audio side renders, 100 frames
// after a sound command. Command 34 writes about 950 AY registers in that
// time (MAME: 977); one register snapshot per audio chunk sees at most 6
// states per frame.
static int register_changes(int cmd, int frames) {
  Arena::init();
  supercobra *m = new supercobra();
  machine = m;
  m->init(&input, frame_buffer, sprites, memory);
  m->reset();
  m->start();

  int changes = 0;
  uint8_t prev[AY_REGS] = {};
  int16_t out[AUDIO_CHUNK];
  for (int f = 0; f < frames; f++) {
    if (f == 20) {
      m->current_cpu = 0;
      m->wrZ80(SOUND_LATCH, cmd);
      m->wrZ80(SOUND_TRIGGER, 0);
      m->wrZ80(SOUND_TRIGGER, TRIGGER_IRQ);
    }
    m->run_frame();
    for (int c = 0; c < CHUNKS_PER_FRAME; c++) {
      m->aylog.play(out, AUDIO_CHUNK, [&](const uint8_t *regs, int16_t *, int) {
        for (int r = 0; r < AY_REGS; r++) {
          changes += regs[r] != prev[r];
          prev[r] = regs[r];
        }
      });
    }
  }
  return changes;
}

#include <sys/wait.h>
#include <unistd.h>
// Sound 34 (helicopter): noise on voice B of AY1 through the 720 Hz RC filter
// the sound CPU selects. Unfiltered it is white noise (mean step / mean
// level about 1.1); MAME's output is dark, well below 0.5.
static float helicopter_roughness(void) {
  Arena::init();
  supercobra *m = new supercobra();
  machine = m;
  m->init(&input, frame_buffer, sprites, memory);
  m->reset();
  m->start();

  int32_t level = 0, steps = 0;
  int16_t prev = 0;
  int16_t out[AUDIO_CHUNK];
  for (int f = 0; f < 120; f++) {
    if (f == 20) {
      m->current_cpu = 0;
      m->wrZ80(SOUND_LATCH, 34);
      m->wrZ80(SOUND_TRIGGER, 0);
      m->wrZ80(SOUND_TRIGGER, TRIGGER_IRQ);
    }
    m->run_frame();
    for (int c = 0; c < CHUNKS_PER_FRAME; c++) {
      m->renderAudio(out, AUDIO_CHUNK);
      if (f < 40) {
        continue;
      }
      for (int i = 0; i < AUDIO_CHUNK; i++) {
        level += abs(out[i]);
        steps += abs(out[i] - prev);
        prev = out[i];
      }
    }
  }
  return level ? (float)steps / level : 99.0f;
}

static void test_helicopter_is_filtered_noise(void) {
  const float roughness = helicopter_roughness();
  TEST_ASSERT_LESS_THAN_FLOAT(0.5f, roughness);
}

static void test_fast_register_writes_are_played(void) {
  TEST_ASSERT_GREATER_THAN(800, register_changes(34, 120));
}
#endif

int main(void) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_helicopter_is_filtered_noise);
  RUN_TEST(test_fast_register_writes_are_played);
#endif
  return UNITY_END();
}
