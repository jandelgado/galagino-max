#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Elevator Action sound: AY bank, DAC on AY#1 port A, d50e mute bit.
// ROM headers come from romconv (./convert.sh elevator); without them
// the test is skipped. Arduino.h / FastLED.h / esp_attr.h are host stubs.
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
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static elevator *machine = nullptr;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}


static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static void create(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new elevator();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

// sound CPU write to AY#chip (1..3) register reg
static void ayWrite(uint8_t chip, uint8_t reg, uint8_t v) {
  const uint16_t base = 0x4800 + 2 * (chip - 1);
  machine->current_cpu = elevator::SOUND_CPU;
  machine->wrZ80(base, reg);
  machine->wrZ80(base + 1, v);
}

static void soundOn(bool on) {
  machine->current_cpu = elevator::MAIN_CPU;
  machine->wrZ80(0xd50e, on ? 0x03 : 0x01);
}

static int32_t peak(void) {
  int16_t out[64];
  int32_t p = 0;
  for (int c = 0; c < 12; c++) {
    machine->renderAudio(out, 64);
    for (int i = 0; i < 64; i++) {
      const int32_t a = out[i] < 0 ? -out[i] : out[i];
      p = a > p ? a : p;
    }
  }
  return p;
}

// AY#3 tone A, period 0x100, volume 15
static void toneOnAy3(void) {
  ayWrite(3, 0, 0x00);
  ayWrite(3, 1, 0x01);
  ayWrite(3, 7, 0x3e);
  ayWrite(3, 8, 0x0f);
}

static void test_tone_on_ay3_audible(void) {
  create();
  machine->reset();
  toneOnAy3();
  soundOn(true);
  const int32_t p = peak();
  printf("AY#3 tone peak %d\n", p);
  TEST_ASSERT_GREATER_THAN_INT32(50, p);
  TEST_ASSERT_LESS_OR_EQUAL_INT32(AUDIO_MAX, p);
}

// d50e b1 = 0 mutes every chip (MAME bankswitch_w output gains)
static void test_mute_bit_silences(void) {
  create();
  machine->reset();
  toneOnAy3();
  soundOn(false);
  TEST_ASSERT_EQUAL_INT32(0, peak());
}

// DAC at full volume (port B 0x00, inverted ladder): data 0xff -> +full
// scale, 0x80 -> about 0
static void test_dac_follows_port_a(void) {
  create();
  machine->reset();
  soundOn(true);
  ayWrite(1, 15, 0x00);
  ayWrite(1, 14, 0xff);
  for (int i = 0; i < 100; i++) {
    machine->pushDac();
  }
  const int32_t high = peak();
  ayWrite(1, 14, 0x80);
  for (int i = 0; i < 100; i++) {
    machine->pushDac();
  }
  const int32_t mid = peak();
  printf("DAC 0xff peak %d, 0x80 peak %d\n", high, mid);
  TEST_ASSERT_GREATER_THAN_INT32(10, high);
  TEST_ASSERT_LESS_THAN_INT32(2, mid);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_tone_on_ay3_audible);
  RUN_TEST(test_mute_bit_silences);
  RUN_TEST(test_dac_follows_port_a);
#endif
  return UNITY_END();
}
