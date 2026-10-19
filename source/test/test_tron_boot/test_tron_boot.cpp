#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots the real Tron ROM on the host through the machine class: CTC IM 2
// interrupts, SSIO sound CPU and latches, render path. Writes one frame to
// $TMPDIR/tron_frame.ppm for a look by eye. ROM headers come from romconv
// (./convert.sh tron) and are not in the repo: without them the test is
// skipped. Arduino.h / FastLED.h / esp_attr.h here are host stubs.
#if __has_include("../../src/machines/tron/tron_rom.h")
#define HAVE_ROM 1
#define ENABLE_TRON
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#define MCR_Z80_TABLES_INCLUDED
#include "../../src/emulation/ay8910.cpp"
#include "../../src/machines/mcr/mcr_ssio.cpp"
#include "../../src/machines/tron/tron.cpp"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static tron *machine;
static uint32_t frame;
static uint32_t watchdogWrites, latchWrites, ip0Reads;

static const uint8_t PORT_WATCHDOG = 0xe0, PORT_LATCH0 = 0x1c, PORT_IP0 = 0x00;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opPrefixZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) {
  if ((p & 0xff) == PORT_IP0) {
    ip0Reads++;
  }
  return machine->inZ80(p);
}
void OutZ80(unsigned short p, unsigned char v) {
  const uint8_t port = p & 0xff;
  if (port == PORT_WATCHDOG) {
    watchdogWrites++;
  }
  if (port >= PORT_LATCH0 && port < PORT_LATCH0 + 4) {
    latchWrites++;
  }
  machine->outZ80(p, v);
}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {} // machineBase::reset(), no 6502 here

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static const uint32_t ATTRACT_FRAMES = 600;
static const uint32_t RENDER_FRAME = 900;
static const int AUDIO_CHUNKS = 6;  // Audio::transmit() per screen

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  machine = new tron();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// one 60 Hz frame as main.cpp drives it; returns the audio peak
static int32_t runFrame(void) {
  machine->run_frame();
  frame++;

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

// attract mode: CTC interrupts drive the game loop, it polls inputs, kicks
// the watchdog, sends sound commands and draws tiles
static void test_attract_runs(void) {
  boot();
  int32_t peak = 0;
  while (frame < ATTRACT_FRAMES) {
    const int32_t p = runFrame();
    peak = p > peak ? p : peak;
  }
  printf("attract: watchdog %u, latch writes %u, IP0 reads %u, audio peak %d\n", watchdogWrites,
         latchWrites, ip0Reads, peak);
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, watchdogWrites);
  TEST_ASSERT_GREATER_THAN_UINT32(0, latchWrites);
  TEST_ASSERT_GREATER_THAN_UINT32(0, ip0Reads);
  TEST_ASSERT_LESS_OR_EQUAL_INT32(AUDIO_MAX, peak);

  uint32_t lit = 0;
  for (uint32_t a = 0xe800; a < 0xef80; a++) {
    lit += machine->rdZ80(a) != 0;
  }
  printf("non-zero video RAM bytes %u\n", lit);
  TEST_ASSERT_GREATER_THAN_UINT32(100, lit);
}

// IP2 holds the joystick as the player sees it (MAME: physical controls are
// not rotated with the screen), so stick directions map 1:1
static void test_stick_not_rotated(void) {
  boot();
  static const struct { unsigned int key; uint8_t bit; } MAP[] = {
      {BUTTON_LEFT, 0x01}, {BUTTON_RIGHT, 0x02}, {BUTTON_UP, 0x04}, {BUTTON_DOWN, 0x08}};
  for (const auto &m : MAP) {
    keys = m.key;
    TEST_ASSERT_EQUAL_HEX8(0xff & ~m.bit, machine->inZ80(0x02));
  }
  keys = 0;
}

// RGB565, bytes swapped for SPI -> 8 bit RGB
static void putPixel(FILE *f, uint16_t v) {
  v = (uint16_t)((v << 8) | (v >> 8));
  const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc),
                          (uint8_t)((v << 3) & 0xf8)};
  fwrite(rgb, 1, 3, f);
}

// one frame through prepare_frame/render_row into a PPM
static void test_render_frame(void) {
  boot();
  while (frame < RENDER_FRAME) {
    runFrame();
  }
  machine->prepare_frame();

  const char *dir = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof(path), "%s/tron_frame.ppm", dir ? dir : "/tmp");
  FILE *f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL(f);
  fprintf(f, "P6\n240 288\n255\n");

  uint32_t lit = 0;
  for (int row = 0; row < 36; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    for (int i = 0; i < 240 * 8; i++) {
      putPixel(f, frame_buffer[i]);
      lit += frame_buffer[i] != 0;
    }
  }
  fclose(f);
  printf("frame %u written to %s, %u non-black px\n", frame, path, lit);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, lit);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_attract_runs);
  RUN_TEST(test_stick_not_rotated);
  RUN_TEST(test_render_frame);
#endif
  return UNITY_END();
}
