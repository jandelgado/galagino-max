#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Toki no Senshi video on the System 2 board (MAME sys2,
// screen_update_system2): fg page 0, 4 bg pages out of 8 chosen by page 0
// bytes 740..746, one x and y scroll, ROT90. MAME attract RAM states
// rendered through the machine class and compared per panel row with the
// MAME snapshot. Needs the romconv headers (./convert.sh tokisens).
// Arduino.h / FastLED.h / esp_attr.h here are host stubs.
#if __has_include("../../src/machines/tokisens/tokisens_main_rom.h")
#define ENABLE_TOKISENS
#endif

#include "../../src/machines/system1/system1_collision.cpp"  // before Z80.h: its `word` macro
#ifdef ENABLE_TOKISENS
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/system1/system1.cpp"
#include "../../src/machines/tokisens/tokisens.cpp"
#undef private
#undef protected

struct MameFrame {
  uint32_t frame;
  uint8_t mode;
  uint8_t vram[0x4000];
  uint8_t pal[0x600];
  uint8_t spr[0x800];
  uint32_t crc[256];  // per panel row 16..271 (native half-res x)
};

#include "mame_tokisens_frames.h"

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
static unsigned short frame_buffer[VIS_LINES * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

static constexpr uint16_t PPI_B_DATA = 0x15;  // video mode
static constexpr int FIRST_ROW = 16;          // native x 0

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

static void bootTokisens() {
  delete machine;
  machine = nullptr;
  Arena::reserve(&tokisens_arena_plan);
  machine = new tokisens();
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// MAME RAM into the machine, latched as at vblank
static void load(const MameFrame &f) {
  memcpy(machine->vramPages, f.vram, sizeof(f.vram));
  memcpy(&memory[system1::SPRITE_RAM], f.spr, sizeof(f.spr));
  machine->current_cpu = system1::MAIN_CPU;
  machine->outZ80(PPI_B_DATA, f.mode);
  for (int i = 0; i < (int)sizeof(f.pal); i++) {
    machine->wrZ80(0xd800 + i, f.pal[i]);
  }
  machine->startVblank();
  machine->prepare_frame();
}

// env TOKISENS_DUMP=<dir>: rendered frames as <dir>/<frame>.ppm for a visual diff
static void dumpRow(FILE *ppm, const uint16_t *row) {
  for (int c = 0; c < VIS_LINES; c++) {
    const uint16_t v = (uint16_t)((row[c] >> 8) | (row[c] << 8));
    const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc),
                            (uint8_t)((v << 3) & 0xf8)};
    fwrite(rgb, 1, 3, ppm);
  }
}

// renders all strips, returns the number of panel rows that differ from MAME
static int compare(const MameFrame &f) {
  int bad = 0;
  FILE *ppm = nullptr;
  if (getenv("TOKISENS_DUMP")) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%u.ppm", getenv("TOKISENS_DUMP"), f.frame);
    ppm = fopen(path, "wb");
    fprintf(ppm, "P6 %d %d 255\n", VIS_LINES, 256);
  }
  for (int strip = machine->firstStrip; strip <= machine->lastStrip; strip++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(strip);
    for (int l = 0; l < 8; l++) {
      const int x = strip * 8 + l - FIRST_ROW;
      if (x < 0 || x >= 256) {
        continue;
      }
      if (ppm) {
        dumpRow(ppm, &frame_buffer[l * VIS_LINES]);
      }
      const uint32_t got = crc32((const uint8_t *)&frame_buffer[l * VIS_LINES],
                                 VIS_LINES * sizeof(uint16_t));
      if (got != f.crc[x]) {
        if (bad == 0) {
          printf("frame %u: first bad row %d\n", f.frame, x + FIRST_ROW);
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

// ROT90 onto the panel: native x 0..255 -> rows 16..271, line y -> column 223 - y
static void test_rot90_strips(void) {
  bootTokisens();
  TEST_ASSERT_EQUAL(VIS_LINES, machine->renderWidth());
  TEST_ASSERT_EQUAL(16 / 8, machine->firstStrip);
  TEST_ASSERT_EQUAL(271 / 8, machine->lastStrip);
}

// peak |sample| of one audio chunk
static int audioPeak() {
  int16_t out[AUDIO_CHUNK];
  machine->renderAudio(out, AUDIO_CHUNK);
  int peak = 0;
  for (int i = 0; i < AUDIO_CHUNK; i++) {
    peak = abs(out[i]) > peak ? abs(out[i]) : peak;
  }
  return peak;
}

// real ROMs, no input, 90 s: MC-8123 code runs from all banks, the attract
// demo scrolls through bg pages 1..5 (MAME probe) with sound. env
// TOKISENS_DUMP: a frame every 10 s as <dir>/boot<frame>.ppm.
static void test_boots_attract(void) {
  bootTokisens();
  uint8_t pagesSeen = 0;
  int peak = 0;
  for (int frame = 1; frame <= 5400; frame++) {
    machine->run_frame();
    for (int c = 0; c < 6; c++) {
      const int p = audioPeak();
      peak = p > peak ? p : peak;
    }
    machine->prepare_frame();
    for (int p = 0; p < BG_PAGES; p++) {
      pagesSeen |= 1 << machine->video.layers.bgPage[p];
    }
    if (frame % 600 == 0 && getenv("TOKISENS_DUMP")) {
      MameFrame f = {};
      f.frame = 100000 + frame;
      compare(f);
    }
  }
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_GREATER_THAN(0, peak);
  TEST_ASSERT_EQUAL_HEX8(0x3e, pagesSeen & 0x3e);
}

// title 180 and attract scrolling 1500, 2220, 3000, boss 3900
static void test_render_matches_mame(void) {
  bootTokisens();
  int bad = 0;
  for (const MameFrame &f : MAME_TOKISENS) {
    load(f);
    bad += compare(f);
  }
  TEST_ASSERT_EQUAL_MESSAGE(0, bad, "tokisens rows differ from MAME");
}
#endif

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
#ifdef ENABLE_TOKISENS
  Arena::init();
#endif
  UNITY_BEGIN();
#ifdef ENABLE_TOKISENS
  RUN_TEST(test_rot90_strips);
  RUN_TEST(test_render_matches_mame);
  RUN_TEST(test_boots_attract);
#endif
  return UNITY_END();
}
