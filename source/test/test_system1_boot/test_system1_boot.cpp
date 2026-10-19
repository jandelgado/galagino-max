#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unity.h>

// System 1 sets boot from their real ROMs: coin + start, 1200 frames, then
// the game must have polled its inputs, drawn to video RAM and played
// sound. Catches wrong decryption, banking or ROM order early. Each set
// needs its romconv headers (./convert.sh <game>), else it is skipped.
// Arduino.h / FastLED.h / esp_attr.h here are host stubs.
#if __has_include("../../src/machines/mrviking/mrviking_main_rom.h")
#define ENABLE_MRVIKING
#endif
#if __has_include("../../src/machines/imsorry/imsorry_main_rom.h")
#define ENABLE_IMSORRY
#endif
#if __has_include("../../src/machines/wboy/wboy_main_rom.h")
#define ENABLE_WBOY
#endif

#include "../../src/machines/system1/system1_collision.cpp"  // before Z80.h: its `word` macro
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/system1/system1.cpp"
#include "../../src/machines/mrviking/mrviking.cpp"
#ifdef ENABLE_IMSORRY
#include "../../src/machines/imsorry/imsorry.cpp"
#endif
#ifdef ENABLE_WBOY
#include "../../src/machines/wboy/wboy.cpp"
#endif
#undef private
#undef protected

static system1 *machine;
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

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

static Input input;
static unsigned short frame_buffer[256 * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

template <class M> static void boot() {
  static bool arena = false;
  delete machine;
  machine = nullptr;
  if (!arena) {
    Arena::init();
    arena = true;
  }
  Arena::reset();
  machine = new M();
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->start();
  machine->start();
}

struct BootResult {
  bool started;
  int vramBytes;  // non-zero bytes in video RAM
  int peak;       // peak |sample|
};

template <class M> static BootResult run() {
  boot<M>();
  BootResult r = {false, 0, 0};
  for (int frame = 0; frame < 1200; frame++) {
    keys = frame >= 300 && frame < 310 ? BUTTON_COIN : 0;
    keys |= frame >= 400 && frame < 410 ? BUTTON_START : 0;
    keys |= frame >= 500 && frame % 40 < 20 ? BUTTON_FIRE : 0;
    machine->run_frame();
    for (int c = 0; c < 6; c++) {
      int16_t out[64];
      machine->renderAudio(out, 64);
      for (int i = 0; i < 64; i++) {
        r.peak = abs(out[i]) > r.peak ? abs(out[i]) : r.peak;
      }
    }
  }
  keys = 0;
  r.started = machine->game_started;
  for (int i = 0; i < 0x1000; i++) {
    r.vramBytes += memory[system1::VIDEO_RAM + i] != 0;
  }
  return r;
}

template <class M> static void check() {
  const BootResult r = run<M>();
  TEST_ASSERT_TRUE(r.started);
  TEST_ASSERT_GREATER_THAN(256, r.vramBytes);
  TEST_ASSERT_GREATER_THAN(0, r.peak);
}

#ifdef ENABLE_MRVIKING
static void test_mrviking_boots(void) { check<mrviking>(); }
#endif
#ifdef ENABLE_IMSORRY
static void test_imsorry_boots(void) { check<imsorry>(); }
#endif
#ifdef ENABLE_WBOY
static void test_wboy_boots(void) { check<wboy>(); }
#endif

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
#ifdef ENABLE_MRVIKING
  RUN_TEST(test_mrviking_boots);
#endif
#ifdef ENABLE_IMSORRY
  RUN_TEST(test_imsorry_boots);
#endif
#ifdef ENABLE_WBOY
  RUN_TEST(test_wboy_boots);
#endif
  return UNITY_END();
}
