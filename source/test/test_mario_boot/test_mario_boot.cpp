#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots the real Mario Bros. ROMs on the host through the machine class
// and plays: coin, start, walk right. ROM headers come from romconv
// (./convert.sh mario); without them the tests are skipped.
#if __has_include("../../src/machines/mario/mario_rom_main.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#define MARIO_Z80_TABLES_INCLUDED
#define private public
#include "../../src/machines/mario/mario.cpp"
#undef private
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
static mario *machine;

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
static unsigned short frame_buffer[mario_video::OUT_WIDTH * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new mario();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void run(uint32_t frames, unsigned int pressed) {
  keys = pressed;
  for (uint32_t i = 0; i < frames; i++) {
    machine->run_frame();
    machine->prepare_frame();
  }
  keys = 0;
}

static void test_boots_into_attract(void) {
  boot();
  run(600, 0);
  TEST_ASSERT_TRUE(machine->game_started);
  // the board runs flipped (monitor mounted upside down)
  TEST_ASSERT_TRUE(machine->video.latch & (1 << mario::LATCH_FLIP));
  TEST_ASSERT_TRUE(machine->video.latch & (1 << mario::LATCH_NMI));
}

static void test_coin_start_walk(void) {
  boot();
  run(5, BUTTON_COIN);
  run(60, 0);
  run(5, BUTTON_START);
  run(900, 0);
  const uint8_t x0 = memory[mario::PLAYER1_X];
  run(30, BUTTON_RIGHT);
  const uint8_t x1 = memory[mario::PLAYER1_X];
  char msg[40];
  snprintf(msg, sizeof(msg), "player 1 x %02x -> %02x", x0, x1);
  TEST_MESSAGE(msg);
  // flipped board: walking right lowers the sprite x byte
  TEST_ASSERT_TRUE(x1 < x0);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boots_into_attract);
  RUN_TEST(test_coin_start_walk);
#endif
  return UNITY_END();
}
