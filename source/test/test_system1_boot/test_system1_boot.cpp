#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unity.h>

// System 1 sets boot from their real ROMs: coin + start, 1200 frames (or more), then
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
#if __has_include("../../src/machines/fourdwarrio/fourdwarrio_main_rom.h")
#define ENABLE_FOURDWARRIO
#endif
#if __has_include("../../src/machines/hvymetal/hvymetal_main_rom.h")
#define ENABLE_HVYMETAL
#endif
#if __has_include("../../src/machines/regulus/regulus_main_rom.h")
#define ENABLE_REGULUS
#endif
#if __has_include("../../src/machines/gardia/gardia_main_rom.h")
#define ENABLE_GARDIA
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
#ifdef ENABLE_FOURDWARRIO
#include "../../src/machines/fourdwarrio/fourdwarrio.cpp"
#endif
#ifdef ENABLE_HVYMETAL
#include "../../src/machines/hvymetal/hvymetal.cpp"
#endif
#ifdef ENABLE_REGULUS
#include "../../src/machines/regulus/regulus.cpp"
#endif
#ifdef ENABLE_GARDIA
#include "../../src/machines/gardia/gardia.cpp"
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

// romconv Arena plan per machine (<game>_arena_plan.h), reserved like switchTo()
template <class M> const ArenaPlan *planOf();
template <> const ArenaPlan *planOf<mrviking>() { return &mrviking_arena_plan; }
template <> const ArenaPlan *planOf<regulus>() { return &regulus_arena_plan; }
template <> const ArenaPlan *planOf<fourdwarrio>() { return &fourdwarrio_arena_plan; }
template <> const ArenaPlan *planOf<gardia>() { return &gardia_arena_plan; }
template <> const ArenaPlan *planOf<hvymetal>() { return &hvymetal_arena_plan; }
template <> const ArenaPlan *planOf<imsorry>() { return &imsorry_arena_plan; }
template <> const ArenaPlan *planOf<wboy>() { return &wboy_arena_plan; }

template <class M> static void boot() {
  static bool arena = false;
  delete machine;
  machine = nullptr;
  if (!arena) {
    Arena::init();
    arena = true;
  }
  Arena::reserve(planOf<M>());
  machine = new M();
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->start();
  machine->start();
}

struct BootResult {
  bool started;
  int vramBytes;  // most non-zero video RAM bytes seen (sampled once a second)
  int peak;       // peak |sample|
};

template <class M> static BootResult run(int frames) {
  boot<M>();
  BootResult r = {false, 0, 0};
  for (int frame = 0; frame < frames; frame++) {
    // coin + start every 10 s: sets with a long self test poll late
    keys = frame % 600 >= 300 && frame % 600 < 310 ? BUTTON_COIN : 0;
    keys |= frame % 600 >= 400 && frame % 600 < 410 ? BUTTON_START : 0;
    keys |= frame >= 500 && frame % 40 < 20 ? BUTTON_FIRE : 0;
    machine->run_frame();
    if (frame % 60 == 0) {
      int n = 0;
      for (int i = 0; i < 0x1000; i++) {
        n += memory[system1::VIDEO_RAM + i] != 0;
      }
      r.vramBytes = n > r.vramBytes ? n : r.vramBytes;
    }
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
  return r;
}

template <class M> static void check(int frames = 1200) {
  const BootResult r = run<M>(frames);
  TEST_ASSERT_TRUE(r.started);
  TEST_ASSERT_GREATER_THAN(256, r.vramBytes);
  TEST_ASSERT_GREATER_THAN(0, r.peak);
}

#ifdef ENABLE_MRVIKING
static void test_mrviking_boots(void) { check<mrviking>(); }

// MAME mrviking: BUTTON1 (attack) on P1 bit 1, FIRE must press it
static void test_mrviking_fire_bit(void) {
  boot<mrviking>();
  keys = BUTTON_FIRE;
  TEST_ASSERT_EQUAL_HEX8(0xfd, machine->inZ80(0x00));
  keys = 0;
}
#endif
#ifdef ENABLE_IMSORRY
static void test_imsorry_boots(void) { check<imsorry>(); }
#endif
#ifdef ENABLE_WBOY
static void test_wboy_boots(void) { check<wboy>(); }
#endif
#ifdef ENABLE_FOURDWARRIO
static void test_fourdwarrio_boots(void) { check<fourdwarrio>(); }
#endif
#ifdef ENABLE_HVYMETAL
static void test_hvymetal_boots(void) { check<hvymetal>(); }

// frames until the first input read
static int framesToStart() {
  int frames = 0;
  while (!machine->game_started && frames < 6000) {
    machine->run_frame();
    frames++;
  }
  return frames;
}

// RAM test and ROM checksum (MAME: first input read at frame 2840, here
// 4354) run on every power-on: reset skips them
static void test_hvymetal_skips_ram_test(void) {
  boot<hvymetal>();
  TEST_ASSERT_LESS_THAN(10, framesToStart());
}

// main CPU RAM 1200 frames into attract: same as after the test (the
// sound CPU idles from power-on, its counters depend on the boot length)
static void test_hvymetal_skip_matches_cold_boot(void) {
  static uint8_t cold[system1::SOUND_RAM];
  boot<hvymetal>();
  machine->system1::reset();
  TEST_ASSERT_GREATER_THAN(1000, framesToStart());
  for (int frame = 0; frame < 1200; frame++) {
    machine->run_frame();
  }
  memcpy(cold, memory, sizeof(cold));

  boot<hvymetal>();
  framesToStart();
  for (int frame = 0; frame < 1200; frame++) {
    machine->run_frame();
  }
  TEST_ASSERT_EQUAL_HEX8_ARRAY(cold, memory, sizeof(cold));
}
#endif
#ifdef ENABLE_GARDIA
// 317-0006 decryption, bank44 banks in the Arena
static void test_gardia_boots(void) { check<gardia>(); }
#endif
#ifdef ENABLE_REGULUS
static void test_regulus_boots(void) { check<regulus>(); }

// main CPU raises the first sound NMI in its PPI init: the sound CPU must
// have set SP (8000-87ff) by then, else its NMI handler returns into RAM
static void test_regulus_sound_stack(void) {
  boot<regulus>();
  for (int frame = 0; frame < 10; frame++) {
    machine->run_frame();
  }
  const uint16_t sp = machine->cpu[system1::SOUND_CPU].SP.W;
  TEST_ASSERT_TRUE(sp >= SOUND_ROM_END && sp <= SOUND_ROM_END + SOUND_RAM_MASK);
}
#endif

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
#ifdef ENABLE_MRVIKING
  RUN_TEST(test_mrviking_boots);
  RUN_TEST(test_mrviking_fire_bit);
#endif
#ifdef ENABLE_IMSORRY
  RUN_TEST(test_imsorry_boots);
#endif
#ifdef ENABLE_WBOY
  RUN_TEST(test_wboy_boots);
#endif
#ifdef ENABLE_FOURDWARRIO
  RUN_TEST(test_fourdwarrio_boots);
#endif
#ifdef ENABLE_HVYMETAL
  RUN_TEST(test_hvymetal_boots);
  RUN_TEST(test_hvymetal_skips_ram_test);
  RUN_TEST(test_hvymetal_skip_matches_cold_boot);
#endif
#ifdef ENABLE_GARDIA
  RUN_TEST(test_gardia_boots);
#endif
#ifdef ENABLE_REGULUS
  RUN_TEST(test_regulus_boots);
  RUN_TEST(test_regulus_sound_stack);
#endif
  return UNITY_END();
}
