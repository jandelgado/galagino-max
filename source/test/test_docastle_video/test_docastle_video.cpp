#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Renders MAME docastle RAM dumps through docastle::render_row() and
// compares every visible line with MAME's snapshot (mame_frames.h, from
// romconv/dev/docastle). ROM headers come from romconv (./convert.sh
// docastle): without them the test is skipped.
#if __has_include("../../src/machines/docastle/docastle_main_rom.h")
#define HAVE_ROM 1
#define ENABLE_DOCASTLE
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/sn76489.cpp"
#include "../../src/cpus/z80/Z80.c"
#define DOCASTLE_Z80_TABLES_INCLUDED
// tests publish video state and read the flip flag
#define private public
#define protected public
#include "../../src/machines/docastle/docastle.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }

static docastle *machine;

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

static Input input;
static unsigned short frame_buffer[224 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint16_t screen[288][224];

static const int PANEL_ROW0 = 24, PANEL_COL0 = 16, VIEW_LINES = 240, WIDTH = 192;

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xffffffff;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
  }
  return ~c;
}

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  Arena::reserve(&docastle_arena_plan);
  machine = new docastle();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

static void renderFrame(const MameFrame &m) {
  machine->vblank.publish([&m](docastle::VideoState &v) {
    memcpy(v.video, m.video, sizeof(v.video));
    memcpy(v.color, m.video + sizeof(v.video), sizeof(v.color));
    memcpy(v.sprites, m.sprites, sizeof(v.sprites));
  });
  machine->prepare_frame();
  for (int row = 0; row < 36; row++) {
    machine->render_row(row);
    memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
  }
}

// $TMPDIR/docastle_<frame>.ppm, mismatching lines marked red at the left
static void dump(const MameFrame &m, const bool *bad) {
  const char *dir = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof(path), "%s/docastle_%u.ppm", dir ? dir : "/tmp", m.frame);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n224 288\n255\n");
  for (int y = 0; y < 288; y++) {
    for (int x = 0; x < 224; x++) {
      uint16_t v = screen[y][x];
      v = (uint16_t)((v << 8) | (v >> 8));
      uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc), (uint8_t)((v << 3) & 0xf8)};
      if (x < 4 && y >= PANEL_ROW0 && y < PANEL_ROW0 + VIEW_LINES && bad[y - PANEL_ROW0]) {
        rgb[0] = 0xff, rgb[1] = rgb[2] = 0;
      }
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
  printf("frame %u written to %s\n", m.frame, path);
}

static void check(int i) {
  boot();
  const MameFrame &m = mame_frames[i];
  renderFrame(m);
  bool bad[VIEW_LINES];
  int mismatches = 0;
  for (int y = 0; y < VIEW_LINES; y++) {
    const uint32_t c = crc32((const uint8_t *)&screen[PANEL_ROW0 + y][PANEL_COL0], WIDTH * 2);
    bad[y] = c != m.line_crc[y];
    mismatches += bad[y];
  }
  if (mismatches) {
    dump(m, bad);
  }
  printf("frame %u: %d of %d lines differ\n", m.frame, mismatches, VIEW_LINES);
  TEST_ASSERT_EQUAL_INT(0, mismatches);
}

static void test_frame_400(void) { check(0); }   // title
static void test_frame_700(void) { check(1); }   // instructions
static void test_frame_910(void) { check(2); }   // overlapping sprites
static void test_frame_1040(void) { check(3); }  // overlapping sprites
static void test_frame_1270(void) { check(4); }
static void test_frame_1500(void) { check(5); }  // high scores
static void test_frame_1700(void) { check(6); }
// sprites overlapping: the first drawn (offs 0x1fc) wins, pen 15 is mask only
static void test_frame_820(void) { check(7); }
static void test_frame_1110(void) { check(8); }
static void test_frame_1210(void) { check(9); }

// columns and rows outside the 192x240 view stay black
static void test_borders_black(void) {
  boot();
  renderFrame(mame_frames[1]);
  for (int y = 0; y < 288; y++) {
    for (int x = 0; x < 224; x++) {
      const bool inside = y >= PANEL_ROW0 && y < PANEL_ROW0 + VIEW_LINES && x >= PANEL_COL0 &&
                          x < PANEL_COL0 + WIDTH;
      if (!inside) {
        TEST_ASSERT_EQUAL_UINT16(0, screen[y][x]);
      }
    }
  }
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_frame_400);
  RUN_TEST(test_frame_700);
  RUN_TEST(test_frame_910);
  RUN_TEST(test_frame_1040);
  RUN_TEST(test_frame_1270);
  RUN_TEST(test_frame_1500);
  RUN_TEST(test_frame_1700);
  RUN_TEST(test_frame_820);
  RUN_TEST(test_frame_1110);
  RUN_TEST(test_frame_1210);
  RUN_TEST(test_borders_black);
#endif
  return UNITY_END();
}
