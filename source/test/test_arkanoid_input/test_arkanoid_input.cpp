#include <string.h>
#include <unity.h>

// Boots the arkangc ROM, starts a game and drives the paddle from the
// stick. ROM headers come from romconv (./convert.sh arkanoid); without
// them the tests are skipped.
#if __has_include("../../src/machines/arkanoid/arkanoid_rom.h")
#define HAVE_ROM 1
#define ENABLE_ARKANOID
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/arkanoid/arkanoid.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int VAUS_SPRITE = 0;        // sprites 0 and 1: the Vaus halves
static const int ATTRACT_FRAMES = 300, PLAY_FRAMES = 1200;
static Input input;
static unsigned int keys = 0;
static unsigned short frame_buffer[224 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static arkanoid *machine;

unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }
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

static void frames(int n, unsigned int k) {
  keys = k;
  for (int f = 0; f < n; f++) {
    machine->run_frame();
  }
  keys = 0;
}

// Vaus panel column: native Y = 248 - s[1], column = 239 - native Y
static int vaus(void) { return memory[ARKANOID_SPR + 4 * VAUS_SPRITE + 1] - 9; }

// cold boot, coin, start, until the Vaus is on screen
static void play(void) {
  if (!machine) {
    Arena::init();
    machine = new arkanoid();
    machine->init(&input, frame_buffer, sprites, memory);
  }
  machine->start();
  frames(ATTRACT_FRAMES, 0);
  frames(5, BUTTON_COIN);
  frames(30, 0);
  frames(5, BUTTON_START);
  frames(PLAY_FRAMES, 0);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(0, memory[ARKANOID_SPR + 4 * VAUS_SPRITE], "no Vaus sprite");
}

// stick right moves the Vaus right on the panel, left moves it left
static void test_paddle_direction(void) {
  play();
  const int start = vaus();
  frames(10, BUTTON_RIGHT);
  const int right = vaus();
  frames(20, BUTTON_LEFT);
  const int left = vaus();
  TEST_ASSERT_GREATER_THAN(start, right);
  TEST_ASSERT_LESS_THAN(right, left);
}

// a tap moves one count (2 px); held, the Vaus accelerates and crosses
// the playfield (left wall to right wall, 179 px) in 0.3 .. 0.7 s
static void test_paddle_speed(void) {
  play();
  frames(60, BUTTON_LEFT);
  frames(10, 0);
  const int wall = vaus();
  frames(1, BUTTON_RIGHT);
  frames(2, 0);   // the game may apply the read a frame late
  const int tap = vaus();
  TEST_ASSERT_GREATER_THAN(wall, tap);
  TEST_ASSERT_LESS_OR_EQUAL(wall + 4, tap);

  frames(18, BUTTON_RIGHT);
  frames(2, 0);
  TEST_ASSERT_LESS_THAN(wall + 140, vaus());   // not across after 0.3 s

  frames(22, BUTTON_RIGHT);
  frames(2, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(wall + 140, vaus());
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_paddle_direction);
  RUN_TEST(test_paddle_speed);
#endif
  return UNITY_END();
}
