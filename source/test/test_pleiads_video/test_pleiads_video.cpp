#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

// Renders MAME pleiads frames from their RAM and compares every visible
// line with MAME's snapshot. ROM headers come from romconv
// (./convert.sh pleiads); without them the tests are skipped.
#if __has_include("../../src/machines/pleiads/pleiads_rom.h")
#define HAVE_ROM 1
#define ENABLE_PLEIADS
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/machines/pleiads/pleiads_sound.cpp"
#define private public
#define protected public
#include "../../src/machines/pleiads/pleiads.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int WIDTH = 224, HEIGHT = 288, ROWS = HEIGHT / 8;
static const int TOP = 16, VISIBLE = 256;   // panel rows of MAME's 208x256 view
static const int LEFT = 8, MAME_W = 208;   // panel columns of it
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static pleiads *machine;

unsigned int Input::buttons_get(void) { return 0; }
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

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t crc = ~0u;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new pleiads();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->game_started = 1;
}

enum SpriteRam { SPRITES_SAME_FRAME, SPRITES_PREV_FRAME };

// current video RAM page 0x4000-0x4FFF as the game wrote it, video
// register and scroll (taps 5000 5800), vblank latch, render. Sprites:
// none, prev is unused.
static void load(const MameFrame &m, SpriteRam) {
  machine->write_videoreg(m.taps[0]);
  memcpy(memory + machine->page * pleiads::PAGE, m.ram, sizeof(m.ram));
  machine->scroll = m.taps[1];
  machine->publish_video();
  machine->prepare_frame();
}

static void render(void) {
  for (int row = 0; row < ROWS; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[8 * row], frame_buffer, sizeof(frame_buffer));
  }
}

static int badLines(const MameFrame &m, SpriteRam spr, int *first) {
  load(m, spr);
  render();
  int bad = 0;
  *first = -1;
  for (int y = 0; y < VISIBLE; y++) {
    if (crc32((const uint8_t *)&screen[TOP + y][LEFT], MAME_W * 2) != m.line_crc[y]) {
      bad++;
      *first = *first < 0 ? y : *first;
    }
  }
  return bad;
}

// MAME may draw sprites from this or the previous frame's RAM: accept either
static void checkFrame(const MameFrame &m) {
  boot();
  int first = -1;
  if (badLines(m, SPRITES_SAME_FRAME, &first) == 0) {
    return;
  }
  const int bad = badLines(m, SPRITES_PREV_FRAME, &first);
  if (bad) {
    char name[64];
    snprintf(name, sizeof(name), "render_%u.raw", m.frame);
    FILE *f = fopen(name, "wb");
    fwrite(screen[TOP], sizeof(screen[0]) * VISIBLE, 1, f);
    fclose(f);
    char msg[96];
    snprintf(msg, sizeof(msg), "frame %u: %d lines differ, first %d (dump %s)", m.frame, bad, first, name);
    TEST_FAIL_MESSAGE(msg);
  }
}

// Boot the real ROMs from reset through both CPUs: the static title
// screen must render as MAME's snapshot of the same frame.
static void test_boot_title(void) {
  boot();
  machine->reset();
  machine->game_started = 1;
  const MameFrame &m = MAME_FRAMES[0];
  for (uint32_t f = 0; f < m.frame; f++) {
    machine->run_frame();
  }
  machine->prepare_frame();
  render();
  int bad = 0;
  for (int y = 0; y < VISIBLE; y++) {
    bad += crc32((const uint8_t *)&screen[TOP + y][LEFT], MAME_W * 2) != m.line_crc[y];
  }
  TEST_ASSERT_EQUAL_INT(0, bad);
}

// The attract demo runs in lockstep with MAME through frame 1800 only with
// the right protection answer polarity (IN0 bit 3).
static void test_boot_attract(void) {
  boot();
  machine->reset();
  machine->game_started = 1;
  int k = 0;
  for (uint32_t f = 1; f <= 1800; f++) {
    machine->run_frame();
    while (k < 5 && MAME_FRAMES[k].frame < f) { k++; }
    if (k < 5 && MAME_FRAMES[k].frame == f) {
      machine->prepare_frame();
      render();
      int bad = 0;
      for (int y = 0; y < VISIBLE; y++) {
        bad += crc32((const uint8_t *)&screen[TOP + y][LEFT], MAME_W * 2) != MAME_FRAMES[k].line_crc[y];
      }
      char msg[48];
      snprintf(msg, sizeof(msg), "frame %u", f);
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad, msg);
    }
  }
}

// Sound envelope of the attract run (std dev per 0.1 s, AUDIO_MAX scale)
// against MAME's -wavwrite at 24 kHz: silent title, sound board noise at
// ~24 s. MAME runs 61.04 Hz, so audio is rendered at that frame rate.
static const int ENV_WINDOW = 2400;
static const uint8_t MAME_ENVELOPE[] = {95,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,73,64,63,88,79,62,68,61,81,82,73,90,64,74,83,78,95,62,67,89,75,73,72,73,72,76,97,100,89,103,60,63,125,116,120,124,115,107,108,105,102,102,95,0,0,0,0,0,0,0,0,0,0,0,0,36};
static const int ENV_COUNT = sizeof(MAME_ENVELOPE);

static void test_audio_envelope(void) {
  boot();
  machine->reset();
  machine->game_started = 1;
  static int16_t audio[ENV_WINDOW * (ENV_COUNT + 1)];
  int total = 0;
  double acc = 0;
  for (uint32_t fr = 1; fr <= 1800 && total < ENV_WINDOW * ENV_COUNT; fr++) {
    machine->run_frame();
    acc += 24000.0 / 61.04;
    while (acc >= 1 && total < ENV_WINDOW * ENV_COUNT) {
      const int n = acc >= AUDIO_CHUNK ? AUDIO_CHUNK : (int)acc;
      machine->renderAudio(audio + total, n);
      total += n;
      acc -= n;
    }
  }
  double env[ENV_COUNT], ma = 0, mb = 0;
  for (int w = 0; w < ENV_COUNT; w++) {
    const int16_t *x = audio + w * ENV_WINDOW;
    double mu = 0, var = 0;
    for (int i = 0; i < ENV_WINDOW; i++) { mu += x[i]; }
    mu /= ENV_WINDOW;
    for (int i = 0; i < ENV_WINDOW; i++) { var += (x[i] - mu) * (x[i] - mu); }
    env[w] = sqrt(var / ENV_WINDOW);
    ma += MAME_ENVELOPE[w];
    mb += env[w];
  }
  ma /= ENV_COUNT;
  mb /= ENV_COUNT;
  double cov = 0, va = 0, vb = 0;
  for (int w = 0; w < ENV_COUNT; w++) {
    cov += (MAME_ENVELOPE[w] - ma) * (env[w] - mb);
    va += (MAME_ENVELOPE[w] - ma) * (MAME_ENVELOPE[w] - ma);
    vb += (env[w] - mb) * (env[w] - mb);
  }
  TEST_ASSERT_GREATER_THAN(0, vb);
  const double corr = cov / sqrt(va * vb);
  TEST_ASSERT_TRUE_MESSAGE(corr > 0.95, "envelope correlation with MAME below 0.95");
}

static void test_title_300(void) { checkFrame(MAME_FRAMES[0]); }
static void test_attract_600(void) { checkFrame(MAME_FRAMES[1]); }
static void test_demo_900(void) { checkFrame(MAME_FRAMES[2]); }
static void test_demo_1200(void) { checkFrame(MAME_FRAMES[3]); }
static void test_demo_1800(void) { checkFrame(MAME_FRAMES[4]); }
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boot_title);
  RUN_TEST(test_boot_attract);
  RUN_TEST(test_audio_envelope);
  RUN_TEST(test_title_300);
  RUN_TEST(test_attract_600);
  RUN_TEST(test_demo_900);
  RUN_TEST(test_demo_1200);
  RUN_TEST(test_demo_1800);
#endif
  return UNITY_END();
}
