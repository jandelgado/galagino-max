#include <stdio.h>
#include <stdlib.h>
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
#include "../../src/cpus/i8048/i8048.c"
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
void i8048_port_write(i8048_state_S *s, unsigned char p, unsigned char v) { machine->wrI8048_port(s, p, v); }
unsigned char i8048_port_read(i8048_state_S *s, unsigned char p) { return machine->rdI8048_port(s, p); }
unsigned char i8048_rom_read(i8048_state_S *s, unsigned short a) { return machine->rdI8048_rom(s, a); }
unsigned char i8048_xdm_read(i8048_state_S *s, unsigned char a) { return machine->rdI8048_xdm(s, a); }
void i8048_xdm_write(i8048_state_S *s, unsigned char a, unsigned char v) { machine->wrI8048_xdm(s, a, v); }

static Input input;
static FILE *audio_dump;  // MARIO_AUDIO_DUMP=<file>: raw int16 24 kHz
static unsigned short frame_buffer[mario_video::OUT_WIDTH * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  if (getenv("MARIO_AUDIO_DUMP")) {
    audio_dump = fopen(getenv("MARIO_AUDIO_DUMP"), "wb");
  }
  machine = new mario();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static int16_t audio_min, audio_max;

// one frame as main.cpp drives it, the audio core drains 6 x 64 samples
static void run(uint32_t frames, unsigned int pressed) {
  keys = pressed;
  for (uint32_t i = 0; i < frames; i++) {
    machine->run_frame();
    machine->prepare_frame();
    for (int c = 0; c < 6; c++) {
      int16_t out[AUDIO_CHUNK];
      machine->renderAudio(out, AUDIO_CHUNK);
      if (audio_dump) {
        fwrite(out, sizeof(out), 1, audio_dump);
      }
      for (int k = 0; k < AUDIO_CHUNK; k++) {
        audio_min = out[k] < audio_min ? out[k] : audio_min;
        audio_max = out[k] > audio_max ? out[k] : audio_max;
      }
    }
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
  audio_min = audio_max = 0;
  run(5, BUTTON_COIN);
  run(60, 0);
  // the coin sound comes from the 8035 DAC
  TEST_ASSERT_TRUE(audio_max - audio_min > 100);
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
