#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Sound commands of the main CPU (latch 001c) drive Gryzor.sid: cue table,
// music, stop and effects over music through the machine's renderAudio().
#if __has_include("../../src/machines/contra/contra_sid.h")
#define HAVE_ROM 1
#define ENABLE_CONTRA
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/sid.cpp"
#include "../../src/emulation/sidplayer.cpp"
#define private public
#define protected public
#include "../../src/machines/contra/contra.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return 0; }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) {}
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return 0; }

static const int RATE = 24000, CHUNK = 64;
static Input input;
static unsigned short frame_buffer[224 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static contra *machine;

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new contra();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

static void command(uint8_t cmd) { machine->m6809_write(&machine->main_cpu, 0x1c, cmd); }

// RMS of the last `tail` of `seconds` rendered
static double render(double seconds, double tail) {
  const int n = (int)(seconds * RATE), skip = (int)((seconds - tail) * RATE);
  int16_t buf[CHUNK];
  double sum = 0;
  int count = 0;
  for (int i = 0; i < n; i += CHUNK) {
    machine->renderAudio(buf, CHUNK);
    for (int k = 0; k < CHUNK; k++) {
      if (i + k >= skip) {
        sum += (double)buf[k] * buf[k];
        count++;
      }
    }
  }
  return sqrt(sum / count);
}

// sweep of all 256 commands in MAME: 00 stop, 02-3b effects, 40-4d music
static void test_cue_table(void) {
  TEST_ASSERT_EQUAL(CUE_STOP, contraCue(0x00).kind);
  TEST_ASSERT_EQUAL(CUE_MUSIC, contraCue(0x4d).kind);
  TEST_ASSERT_EQUAL(1, contraCue(0x4d).arg);
  TEST_ASSERT_EQUAL(CUE_MUSIC, contraCue(0x40).kind);
  TEST_ASSERT_EQUAL(2, contraCue(0x40).arg);
  TEST_ASSERT_EQUAL(CUE_FX, contraCue(0x13).kind);
  TEST_ASSERT_EQUAL(CUE_NONE, contraCue(0x2e).kind);
  TEST_ASSERT_EQUAL(CUE_NONE, contraCue(0xff).kind);
}

static void test_music_then_stop(void) {
  boot();
  command(0x4d);
  const double music = render(2.0, 1.0);
  command(0x00);
  const double stopped = render(0.5, 0.2);
  printf("music rms %.0f, stopped %.0f\n", music, stopped);
  TEST_ASSERT_GREATER_THAN(50, (int)music);   // SidPlayer probe: ~130
  TEST_ASSERT_LESS_THAN(5, (int)stopped);
}

// effects play over the running tune through the driver's $04f1 entry
static void test_effect_over_music(void) {
  boot();
  command(0x4d);
  render(0.5, 0.5);
  command(0x13);
  render(0.5, 0.5);
  printf("strays %u\n", machine->player.strays());
  TEST_ASSERT_EQUAL_UINT32(0, machine->player.strays());
}

static void test_effect_without_music(void) {
  boot();
  command(0x00);
  render(0.5, 0.5);
  command(0x27);   // explosion
  const double fx = render(0.3, 0.3);
  printf("effect rms %.0f\n", fx);
  TEST_ASSERT_GREATER_THAN(50, (int)fx);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_cue_table);
  RUN_TEST(test_music_then_stop);
  RUN_TEST(test_effect_over_music);
  RUN_TEST(test_effect_without_music);
#endif
  return UNITY_END();
}
