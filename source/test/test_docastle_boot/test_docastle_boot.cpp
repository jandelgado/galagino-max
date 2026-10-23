#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots the real Mr. Do's Castle ROMs on the host through the machine
// class: main/slave latch handshake, TMS1025 input mux, render path.
// Writes frames to $TMPDIR/docastle_*.ppm for a look by eye. ROM headers
// come from romconv (./convert.sh docastle) and are not in the repo:
// without them the test is skipped. Arduino.h / FastLED.h / esp_attr.h
// here are host stubs.
#if __has_include("../../src/machines/docastle/docastle_main_rom.h")
#define HAVE_ROM 1
#define ENABLE_DOCASTLE
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/sn76489.cpp"
#include "../../src/cpus/z80/Z80.c"
#define DOCASTLE_Z80_TABLES_INCLUDED
// tests read latch and input state
#define private public
#define protected public
#include "../../src/machines/docastle/docastle.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }

static docastle *machine;
static uint32_t frame;

// idle loop entry fetches since boot, per CPU (docastle.cpp *_IDLE_PC)
static uint32_t idleFetches[2];

extern "C" {
unsigned char OpZ80_INL(unsigned short a) {
  const bool slave = machine->current_cpu == SLAVE_CPU;
  if (slave ? a == SLAVE_IDLE_PC : a == MAIN_IDLE_PC || a == MAIN_POLL_PC) {
    idleFetches[slave]++;
  }
  return machine->opZ80(a);
}
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return 0xff; }
void OutZ80(unsigned short p, unsigned char v) {}  // CRTC, fixed by the ROM
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static Input input;
static unsigned short frame_buffer[224 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint16_t screen[288][224];

static const uint32_t ATTRACT_FRAMES = 900;
static const uint32_t COIN_FRAME = 950;
static const uint32_t START_FRAME = 1010;
static const uint32_t PRESS_FRAMES = 5;
static const uint32_t GAME_FRAME = 1100;

// main RAM, found by diffing MAME dumps around a coin and a start
// (RAM_GAME_STATE, STATE_CREDIT: docastle.cpp)
static const uint16_t RAM_CREDITS = 0x8012 - 0x8000;
static const uint8_t STATE_ATTRACT = 0, STATE_PLAYING = 4;
static const int AUDIO_CHUNKS = 6;  // Audio::transmit() per screen

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  Arena::reserve(&docastle_arena_plan);  // like switchTo()
  machine = new docastle();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static uint32_t waitFrames, maxWaitFrames;

// one 60 Hz frame as main.cpp drives it; returns the audio peak
static int32_t runFrame(void) {
  machine->run_frame();
  frame++;
  waitFrames = machine->main_wait ? waitFrames + 1 : 0;
  maxWaitFrames = waitFrames > maxWaitFrames ? waitFrames : maxWaitFrames;


  int32_t peak = 0;
  for (int c = 0; c < AUDIO_CHUNKS; c++) {
    int16_t out[64];
    machine->renderAudio(out, 64);
    for (int i = 0; i < 64; i++) {
      const int32_t a = out[i] < 0 ? -out[i] : out[i];
      peak = a > peak ? a : peak;
    }
  }
  return peak;
}

static void runTo(uint32_t f) {
  while (frame < f) {
    runFrame();
  }
}

// RGB565, bytes swapped for SPI -> 8 bit RGB
static void putPixel(FILE *f, uint16_t v) {
  v = (uint16_t)((v << 8) | (v >> 8));
  const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc),
                          (uint8_t)((v << 3) & 0xf8)};
  fwrite(rgb, 1, 3, f);
}

// all 36 strips into screen[], also written as $TMPDIR/<name>.ppm
static void renderScreen(const char *name) {
  machine->prepare_frame();
  for (int row = 0; row < 36; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
  }
  const char *dir = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir ? dir : "/tmp", name);
  FILE *f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL(f);
  fprintf(f, "P6\n224 288\n255\n");
  for (int y = 0; y < 288; y++) {
    for (int x = 0; x < 224; x++) {
      putPixel(f, screen[y][x]);
    }
  }
  fclose(f);
  printf("frame %u written to %s\n", frame, path);
}

static uint32_t litPixels(int row0, int row1) {
  uint32_t lit = 0;
  for (int y = row0; y < row1; y++) {
    for (int x = 0; x < 224; x++) {
      lit += screen[y][x] != 0;
    }
  }
  return lit;
}

// attract: the handshake never stalls main, the game draws tiles and
// sprites and plays sound
static void test_attract_runs(void) {
  boot();
  int32_t peak = 0;
  uint32_t spriteChanges = 0;
  uint8_t lastSprites[0x200];
  memcpy(lastSprites, memory + DOCASTLE_MEM_SPRITES, sizeof(lastSprites));
  while (frame < ATTRACT_FRAMES) {
    const int32_t p = runFrame();
    peak = p > peak ? p : peak;
    spriteChanges += memcmp(lastSprites, memory + DOCASTLE_MEM_SPRITES, sizeof(lastSprites)) != 0;
    memcpy(lastSprites, memory + DOCASTLE_MEM_SPRITES, sizeof(lastSprites));
  }

  printf("attract: sprite changes %u, max wait %u, audio peak %d\n", spriteChanges, maxWaitFrames,
         peak);
  renderScreen("docastle_attract");

  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(1, maxWaitFrames);
  TEST_ASSERT_GREATER_THAN_UINT32(100, spriteChanges);
  TEST_ASSERT_EQUAL_UINT8(STATE_ATTRACT, memory[RAM_GAME_STATE]);
  TEST_ASSERT_EQUAL_UINT32(0, litPixels(0, 24));
  TEST_ASSERT_EQUAL_UINT32(0, litPixels(264, 288));
  TEST_ASSERT_GREATER_THAN_UINT32(5000, litPixels(24, 264));
}

// coin, then start: one credit, then a running game with music
static void test_coin_and_start(void) {
  boot();
  runTo(COIN_FRAME);
  keys = BUTTON_COIN;
  runTo(COIN_FRAME + PRESS_FRAMES);
  keys = 0;
  runTo(START_FRAME);
  TEST_ASSERT_EQUAL_UINT8(1, memory[RAM_CREDITS]);
  TEST_ASSERT_EQUAL_UINT8(STATE_CREDIT, memory[RAM_GAME_STATE]);

  keys = BUTTON_START;
  runTo(START_FRAME + PRESS_FRAMES);
  keys = 0;
  int32_t peak = 0;
  while (frame < GAME_FRAME) {
    const int32_t p = runFrame();
    peak = p > peak ? p : peak;
  }
  printf("game: audio peak %d\n", peak);
  renderScreen("docastle_game");
  TEST_ASSERT_EQUAL_UINT8(0, memory[RAM_CREDITS]);
  TEST_ASSERT_EQUAL_UINT8(STATE_PLAYING, memory[RAM_GAME_STATE]);
  TEST_ASSERT_GREATER_THAN_INT32(0, peak);
  TEST_ASSERT_LESS_OR_EQUAL_INT32(AUDIO_MAX, peak);
}

// TMS1025 mux: a read returns the port the previous read selected
static void test_input_mux(void) {
  boot();
  keys = 0;
  machine->readInputs(2);                                // select DSW1
  TEST_ASSERT_EQUAL_HEX8(DOCASTLE_DSW1, machine->readInputs(1));  // DSW1, select DSW2
  TEST_ASSERT_EQUAL_HEX8(DOCASTLE_DSW2, machine->readInputs(3));
  keys = BUTTON_UP | BUTTON_LEFT;
  TEST_ASSERT_EQUAL_HEX8(0xff & ~0x02 & ~0x04, machine->readInputs(5));  // JOYS
  keys = BUTTON_FIRE;
  TEST_ASSERT_EQUAL_HEX8(0xfe, machine->readInputs(7));  // BUTTONS
  keys = BUTTON_COIN;
  TEST_ASSERT_EQUAL_HEX8(0xdf, machine->readInputs(0x80 | 7));  // SYSTEM, c08x mirror
  keys = BUTTON_START;
  TEST_ASSERT_EQUAL_HEX8(0xff, machine->readInputs(0));  // SYSTEM again: START is BUTTONS
  TEST_ASSERT_EQUAL_HEX8(0xff, machine->readInputs(0));  // select 0 keeps the last data
  keys = 0;  // a button held at boot enters test mode
}

// idle loops are skipped, not stepped: one pass per IRQ (main 1, slave 8
// per frame), not thousands
static void test_idle_skip(void) {
  boot();
  runTo(GAME_FRAME + 120);
  printf("idle fetches per frame: main %u, slave %u\n", idleFetches[MAIN_CPU] / frame,
         idleFetches[SLAVE_CPU] / frame);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(4, idleFetches[MAIN_CPU] / frame);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(24, idleFetches[SLAVE_CPU] / frame);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_input_mux);
  RUN_TEST(test_attract_runs);
  RUN_TEST(test_coin_and_start);
  RUN_TEST(test_idle_skip);
#endif
  return UNITY_END();
}
