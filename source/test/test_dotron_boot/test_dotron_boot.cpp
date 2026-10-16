#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots the real Discs of Tron ROM on the host through the machine class:
// CTC IM 2 interrupts, SSIO sound CPU and latches, render path. Writes
// frames to $TMPDIR/dotron_*.ppm for a look by eye. ROM headers come from
// romconv (./convert.sh dotron) and are not in the repo: without them the
// test is skipped. Arduino.h / FastLED.h / esp_attr.h here are host stubs.
// Frame numbers count 60 Hz fields: MAME frame N (30 Hz) = field 2N.
#if __has_include("../../src/machines/dotron/dotron_rom.h")
#define HAVE_ROM 1
#define ENABLE_DOTRON
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#define MCR_Z80_TABLES_INCLUDED
#include "../../src/emulation/ay8910.cpp"
#include "../../src/machines/mcr/mcr_ssio.cpp"
// tests read sprite and input state
#define private public
#define protected public
#include "../../src/machines/dotron/dotron.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static dotron *machine;
static uint32_t frame;
static uint32_t watchdogWrites, latchWrites, ip0Reads;

static const uint8_t PORT_WATCHDOG = 0xe0, PORT_LATCH0 = 0x1c, PORT_IP0 = 0x00;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
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
static uint16_t screen[288][240];

static const uint32_t TITLE_FRAME = 360;     // MAME 180: "DISCS OF TRON"
static const uint32_t ATTRACT_FRAMES = 2400;
static const uint32_t COIN_FRAME = 6000;     // MAME 3000
static const uint32_t START_FRAME = 6120;    // MAME 3060
static const uint32_t GAME_FRAME = 7800;     // MAME 3900, player on platform
static const uint32_t SCORE_FRAME = 10800;   // MAME 5400
static const uint16_t RAM_SCORE = 0xe025;    // digits, 1 per byte: e025 1e5 .. e028 100
static const int AUDIO_CHUNKS = 6;  // Audio::transmit() per screen

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  machine = new dotron();
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
  fprintf(f, "P6\n240 288\n255\n");
  for (int y = 0; y < 288; y++) {
    for (int x = 0; x < 240; x++) {
      putPixel(f, screen[y][x]);
    }
  }
  fclose(f);
  printf("frame %u written to %s\n", frame, path);
}

static uint32_t litPixels(int row0, int row1) {
  uint32_t lit = 0;
  for (int y = row0; y < row1; y++) {
    for (int x = 0; x < 240; x++) {
      lit += screen[y][x] != 0;
    }
  }
  return lit;
}

// title: text in the upper half, nothing outside the 240 visible rows
static void test_title_frame(void) {
  boot();
  runTo(TITLE_FRAME);
  renderScreen("dotron_title");
  TEST_ASSERT_EQUAL_UINT32(0, litPixels(0, 24));
  TEST_ASSERT_EQUAL_UINT32(0, litPixels(264, 288));
  TEST_ASSERT_GREATER_THAN_UINT32(500, litPixels(40, 140));
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
  for (uint32_t a = 0xf000; a < 0xf780; a++) {
    lit += machine->rdZ80(a) != 0;
  }
  printf("non-zero video RAM bytes %u\n", lit);
  TEST_ASSERT_GREATER_THAN_UINT32(100, lit);
}

// coin + start reach the game: player and enemy sprites on screen
static void test_coin_start_reaches_game(void) {
  boot();
  runTo(COIN_FRAME);
  keys = BUTTON_COIN;
  runTo(COIN_FRAME + 10);
  keys = 0;
  runTo(START_FRAME);
  keys = BUTTON_START;
  runTo(START_FRAME + 10);
  keys = 0;
  runTo(GAME_FRAME);
  renderScreen("dotron_game");
  printf("active sprites %d\n", machine->active_sprites);
  TEST_ASSERT_GREATER_THAN_INT(2, machine->active_sprites);
  TEST_ASSERT_GREATER_THAN_UINT32(2000, litPixels(24, 264));
}

// mean panel column of Tron (blue) pixels, -1 if none
static int blueColumn(void) {
  long sum = 0, n = 0;
  for (int y = 24; y < 264; y++) {
    for (int x = 0; x < 240; x++) {
      const uint16_t v = (uint16_t)((screen[y][x] << 8) | (screen[y][x] >> 8));
      const int r = v >> 11, b = v & 31;
      if (b > 20 && r < 8) {
        sum += x;
        n++;
      }
    }
  }
  return n ? (int)(sum / n) : -1;
}

// the stick moves Tron the way it is pushed on the mirrored screen
static void test_stick_left_moves_left(void) {
  boot();
  renderScreen("dotron_stick0");
  const int before = blueColumn();
  keys = BUTTON_LEFT;
  runTo(frame + 60);
  keys = 0;
  renderScreen("dotron_stick1");
  const int after = blueColumn();
  printf("Tron column %d -> %d\n", before, after);
  TEST_ASSERT_TRUE(before >= 0 && after >= 0);
  TEST_ASSERT_LESS_THAN_INT(before - 4, after);
}

static int score(void) {
  int v = 0;
  for (int i = 0; i < 4; i++) {
    v = v * 10 + machine->rdZ80(RAM_SCORE + i);
  }
  return v * 100;
}

// blind fire twice a second, no aiming: auto-aim hits Sark
static void test_auto_aim_scores(void) {
  boot();
  while (frame < SCORE_FRAME) {
    keys = (frame % 60) < 8 ? BUTTON_FIRE : 0;
    runFrame();
  }
  keys = 0;
  renderScreen("dotron_score");
  printf("score %d\n", score());
  TEST_ASSERT_GREATER_OR_EQUAL_INT(2000, score());
}

#ifdef LED_PIN
static bool sameLeds(const CRGB *a, const CRGB *b) {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b) {
      return false;
    }
  }
  return true;
}

// a new sound command flashes all LEDs for a few frames, then menu colors
static void test_leds_flash_on_sound_command(void) {
  boot();
  CRGB menu[NUM_LEDS], game[NUM_LEDS];
  dotron::menuLeds(menu);
  TEST_ASSERT_FALSE(menu[0].r == menu[3].r && menu[0].g == menu[3].g && menu[0].b == menu[3].b);

  runTo(frame + 10);
  machine->outZ80(PORT_LATCH0, (uint8_t)(machine->last_command + 1));
  machine->gameLeds(game);
  TEST_ASSERT_FALSE(sameLeds(game, menu));
  for (int i = 1; i < NUM_LEDS; i++) {  // one flash color
    TEST_ASSERT_EQUAL_UINT8(game[0].r, game[i].r);
    TEST_ASSERT_EQUAL_UINT8(game[0].g, game[i].g);
    TEST_ASSERT_EQUAL_UINT8(game[0].b, game[i].b);
  }

  keys = 0;
  for (int i = 0; i < 4; i++) {
    machine->run_frame();
  }
  machine->outZ80(PORT_LATCH0, machine->last_command);  // same command: no flash
  machine->gameLeds(game);
  TEST_ASSERT_TRUE(sameLeds(game, menu));
}
#endif
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_title_frame);
  RUN_TEST(test_attract_runs);
  RUN_TEST(test_coin_start_reaches_game);
  RUN_TEST(test_stick_left_moves_left);
  RUN_TEST(test_auto_aim_scores);
#ifdef LED_PIN
  RUN_TEST(test_leds_flash_on_sound_command);
#endif
#endif
  return UNITY_END();
}
