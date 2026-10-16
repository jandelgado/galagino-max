#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// System 1 ROT0 games (Flicky, Pitfall II): MAME RAM states rendered through
// the machine class, each panel line compared with the MAME snapshot at the
// machine's window. Needs the romconv headers (./convert.sh flicky pitfall2).
// Machine state is private: this test opens it up. Arduino.h / FastLED.h /
// esp_attr.h here are host stubs.
#if __has_include("../../src/machines/flicky/flicky_main_rom.h") && \
    __has_include("../../src/machines/pitfall2/pitfall2_main_rom.h")
#define HAVE_ROMS 1
#define ENABLE_FLICKY
#define ENABLE_PITFALL2
#endif
#if defined(HAVE_ROMS) && __has_include("../../src/machines/imsorry/imsorry_main_rom.h")
#define ENABLE_IMSORRY
#endif
#if defined(HAVE_ROMS) && __has_include("../../src/machines/wboy/wboy_main_rom.h")
#define ENABLE_WBOY
#endif
#if defined(HAVE_ROMS) && __has_include("../../src/machines/fourdwarrio/fourdwarrio_main_rom.h")
#define ENABLE_FOURDWARRIO
#endif
#if defined(HAVE_ROMS) && __has_include("../../src/machines/hvymetal/hvymetal_main_rom.h")
#define ENABLE_HVYMETAL
#endif

#ifdef HAVE_ROMS
#include "../../src/machines/system1/system1_collision.cpp"  // before Z80.h: its `word` macro
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/system1/system1.cpp"
#include "../../src/machines/flicky/flicky.cpp"
#include "../../src/machines/pitfall2/pitfall2.cpp"
#ifdef ENABLE_IMSORRY
#include "../../src/machines/imsorry/imsorry.cpp"
#endif
#ifdef ENABLE_WBOY
#include "../../src/machines/wboy/wboy.cpp"
#endif
#ifdef ENABLE_FOURDWARRIO
#include "../../src/machines/fourdwarrio/fourdwarrio.cpp"
#endif
#ifdef ENABLE_HVYMETAL
#include "../../src/machines/hvymetal/hvymetal.cpp"
#endif
#undef private
#undef protected

struct MameFrame {
  uint32_t frame;
  uint8_t vram[0x1000];
  uint8_t pal[0x600];
  uint8_t spr[0x800];
  uint32_t crc[224][17];  // per line, per window start 0..16
};

#include "mame_flicky.h"
#include "mame_pitfall2.h"
#ifdef ENABLE_HVYMETAL
#include "mame_hvymetal.h"
#endif
#ifdef ENABLE_FOURDWARRIO
#include "mame_fourdwarrio.h"
#endif
#ifdef ENABLE_WBOY
#include "mame_wboy.h"
#endif
#ifdef ENABLE_IMSORRY
#include "mame_imsorry.h"
#endif

using namespace system1_video;

static system1 *machine;
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

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
static unsigned short frame_buffer[ROT0_WIDTH * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
  }
  return ~c;
}

// Arena ready before the ctor: it unpacks the ROMs. One Arena per process,
// emptied for the next machine as main's switchTo() does.
template <class M> static void boot() {
  static bool arena = false;
  delete machine;
  machine = nullptr;
  if (!arena) {
    Arena::init();
    arena = true;
  }
  Arena::reset();
  machine = new M();
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// MAME RAM into the machine, latched as at vblank
static void load(const MameFrame &f) {
  memcpy(&memory[system1::VIDEO_RAM], f.vram, sizeof(f.vram));
  memcpy(&memory[system1::SPRITE_RAM], f.spr, sizeof(f.spr));
  machine->current_cpu = system1::MAIN_CPU;
  for (int i = 0; i < (int)sizeof(f.pal); i++) {
    machine->wrZ80(0xd800 + i, f.pal[i]);
  }
  machine->startVblank();
  machine->prepare_frame();
}

// env ROT0_DUMP=<dir>: rendered frames as <dir>/<frame>.ppm for a visual diff
static void dumpLine(FILE *ppm, int y, const uint16_t *line) {
  for (int x = 0; x < ROT0_WIDTH; x++) {
    const uint16_t c = (uint16_t)((line[x] >> 8) | (line[x] << 8));
    const uint8_t rgb[3] = {(uint8_t)((c >> 8) & 0xf8), (uint8_t)((c >> 3) & 0xfc),
                            (uint8_t)((c << 3) & 0xf8)};
    fwrite(rgb, 1, 3, ppm);
  }
}

// renders all strips, returns the number of lines that differ from MAME
static int compare(const MameFrame &f, int winX) {
  int bad = 0;
  FILE *ppm = nullptr;
  if (getenv("ROT0_DUMP")) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%u.ppm", getenv("ROT0_DUMP"), f.frame);
    ppm = fopen(path, "wb");
    fprintf(ppm, "P6 %d %d 255\n", ROT0_WIDTH, VIS_LINES);
  }
  machine->winX = winX;
  for (int strip = machine->firstStrip; strip <= machine->lastStrip; strip++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(strip);
    for (int l = 0; l < 8; l++) {
      const int y = strip * 8 + l - ROT0_FIRST_ROW;
      if (ppm) {
        dumpLine(ppm, y, &frame_buffer[l * ROT0_WIDTH]);
      }

      // HUD lines mix fg at hudX with bg at winX: MAME has no such window
      const bool hud = y < machine->config.window.hudLines;
      if (hud && winX != machine->config.window.hudX) {
        continue;
      }
      const uint32_t got = crc32((const uint8_t *)&frame_buffer[l * ROT0_WIDTH],
                                 ROT0_WIDTH * sizeof(uint16_t));
      if (got != f.crc[y][winX]) {
        if (bad == 0) {
          printf("frame %u: first bad line %d (window %d)\n", f.frame, y, winX);
          fflush(stdout);
        }
        bad++;
      }
    }
  }
  if (ppm) {
    fclose(ppm);
  }
  return bad;
}

void setUp(void) {}
void tearDown(void) {}

static void test_flicky_frames(void) {
  boot<flicky>();
  TEST_ASSERT_EQUAL(ROT0_WIDTH, machine->renderWidth());
  for (const MameFrame &f : MAME_FLICKY) {
    load(f);
    TEST_ASSERT_EQUAL(8, machine->winX);
    TEST_ASSERT_EQUAL_MESSAGE(0, compare(f, 8), "flicky lines differ from MAME");
  }
}

// any window in 5..10 matches MAME (HUD lines checked at their own x 5)
static void test_pitfall2_frames(void) {
  boot<pitfall2>();
  for (const MameFrame &f : MAME_PITFALL2) {
    load(f);
    TEST_ASSERT_EQUAL_MESSAGE(0, compare(f, 5), "pitfall2 lines differ from MAME (window 5)");
    TEST_ASSERT_EQUAL_MESSAGE(0, compare(f, 10), "pitfall2 lines differ from MAME (window 10)");
  }
}

// the window settles where Harry (sprite 0) puts it, 1 px per frame
static void test_pitfall2_window_follows(void) {
  boot<pitfall2>();
  for (const MameFrame &f : MAME_PITFALL2) {
    load(f);
    const int px = machine->playerX();
    TEST_ASSERT_TRUE(px >= 0);
    for (int i = 0; i < 6; i++) {
      machine->prepare_frame();
    }
    TEST_ASSERT_EQUAL(windowTarget(machine->config.window, px), machine->winX);
  }
}

// boots the real ROM, coin + start, returns the peak |sample| of 1200 frames
template <class M> static int audioPeak() {
  boot<M>();
  int peak = 0;
  for (int frame = 0; frame < 1200; frame++) {
    keys = frame >= 300 && frame < 310 ? BUTTON_COIN : 0;
    keys |= frame >= 400 && frame < 410 ? BUTTON_START : 0;
    machine->run_frame();
    for (int c = 0; c < 6; c++) {
      int16_t out[64];
      machine->renderAudio(out, 64);
      for (int i = 0; i < 64; i++) {
        peak = abs(out[i]) > peak ? abs(out[i]) : peak;
      }
    }
  }
  keys = 0;
  return peak;
}

#ifdef ENABLE_IMSORRY
// content x 8..247 (MAME): fixed window 8, no HUD band
static void test_imsorry_frames(void) {
  boot<imsorry>();
  for (const MameFrame &f : MAME_IMSORRY) {
    load(f);
    TEST_ASSERT_EQUAL(8, machine->winX);
    TEST_ASSERT_EQUAL_MESSAGE(0, compare(f, 8), "imsorry lines differ from MAME");
  }
}
#endif

#ifdef ENABLE_WBOY
// content x 16..239 (MAME): fixed window 8 shows all of it
static void test_wboy_frames(void) {
  boot<wboy>();
  for (const MameFrame &f : MAME_WBOY) {
    load(f);
    TEST_ASSERT_EQUAL(8, machine->winX);
    TEST_ASSERT_EQUAL_MESSAGE(0, compare(f, 8), "wboy lines differ from MAME");
  }
}
#endif

#ifdef ENABLE_FOURDWARRIO
// content spans x 0..255 (MAME, scrolling playfield): fixed centered window 8
static void test_fourdwarrio_frames(void) {
  boot<fourdwarrio>();
  for (const MameFrame &f : MAME_FOURDWARRIO) {
    load(f);
    TEST_ASSERT_EQUAL(8, machine->winX);
    TEST_ASSERT_EQUAL_MESSAGE(0, compare(f, 8), "fourdwarrio lines differ from MAME");
  }
}
#endif

#ifdef ENABLE_HVYMETAL
// status panel at x 0..63 (labels from x 0), playfield to x 255 (MAME):
// fixed window 0 keeps the panel whole; title uses the PROM palette
static void test_hvymetal_frames(void) {
  boot<hvymetal>();
  for (const MameFrame &f : MAME_HVYMETAL) {
    load(f);
    TEST_ASSERT_EQUAL(0, machine->winX);
    TEST_ASSERT_EQUAL_MESSAGE(0, compare(f, 0), "hvymetal lines differ from MAME");
  }
}
#endif

static void test_flicky_audio(void) { TEST_ASSERT_GREATER_THAN(0, audioPeak<flicky>()); }
static void test_pitfall2_audio(void) { TEST_ASSERT_GREATER_THAN(0, audioPeak<pitfall2>()); }
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROMS
  RUN_TEST(test_flicky_frames);
  RUN_TEST(test_pitfall2_frames);
  RUN_TEST(test_pitfall2_window_follows);
  RUN_TEST(test_flicky_audio);
  RUN_TEST(test_pitfall2_audio);
#ifdef ENABLE_IMSORRY
  RUN_TEST(test_imsorry_frames);
#endif
#endif
#ifdef ENABLE_WBOY
  RUN_TEST(test_wboy_frames);
#endif
#ifdef ENABLE_FOURDWARRIO
  RUN_TEST(test_fourdwarrio_frames);
#endif
#ifdef ENABLE_HVYMETAL
  RUN_TEST(test_hvymetal_frames);
#endif
  return UNITY_END();
}
