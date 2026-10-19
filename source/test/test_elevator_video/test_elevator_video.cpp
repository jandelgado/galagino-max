#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Elevator Action video: palette, render against MAME reference frames.
// ROM headers come from romconv (./convert.sh elevator); without them
// the test is skipped. Arduino.h / FastLED.h / esp_attr.h are host stubs.
#if __has_include("../../src/machines/elevator/elevator_main_rom.h")
#define HAVE_ROM 1
#define ENABLE_ELEVATOR
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

static elevator *machine;

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
void Reset6502(M6502 *) {}


// MAME set_pens(): inverted bits, 1k/470/270 ohm weights, RGB565 byte-swapped
static void test_palette_matches_mame(void) {
  TEST_ASSERT_EQUAL_HEX16(0xffff, elevator::pen(0x00, 0x00));  // white
  TEST_ASSERT_EQUAL_HEX16(0x0000, elevator::pen(0x01, 0xff));  // black
  TEST_ASSERT_EQUAL_HEX16(0x00f8, elevator::pen(0x00, 0x3f));  // red 255
  TEST_ASSERT_EQUAL_HEX16(0x1f00, elevator::pen(0x01, 0xf8));  // blue 255
  TEST_ASSERT_EQUAL_HEX16(0x0020, elevator::pen(0x01, 0xbf));  // red 37 (1 kohm only)
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_palette_matches_mame);
#endif
  return UNITY_END();
}
