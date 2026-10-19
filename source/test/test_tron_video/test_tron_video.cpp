#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Renders MAME tron frames from their RAM and palette and compares with
// MAME's snapshot halved to the 240x256 panel view. Tiles are doubled by
// the hardware, so tile pixels must match exactly; sprites are downsampled
// 2:1 and may lose single full-res pixels. ROM headers come from romconv
// (./convert.sh tron); without them the tests are skipped.
#if __has_include("../../src/machines/tron/tron_rom.h")
#define HAVE_ROM 1
#define ENABLE_TRON
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#define MCR_Z80_TABLES_INCLUDED
#include "../../src/emulation/ay8910.cpp"
#include "../../src/machines/mcr/mcr_ssio.cpp"
#define private public
#define protected public
#include "../../src/machines/tron/tron.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int WIDTH = 240, HEIGHT = 288, TOP = 16, VISIBLE = 256;
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static tron *machine;

unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short) { return 0xff; }
void OutZ80(unsigned short, unsigned char) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new tron();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

// render MAME's RAM through publish/prepare/render_row
static void render(const MameFrame &m) {
  machine->reset();
  memcpy(memory + TRON_MEM_SPRITES, m.ram, 0x200);
  memcpy(memory + TRON_MEM_VIDEO, m.ram + 0x800, 0x800);
  memcpy(machine->palette9, m.pal9, sizeof(m.pal9));
  machine->publish_video();
  machine->prepare_frame();
  for (int row = 0; row < HEIGHT / 8; row++) {
    machine->render_row(row);
    memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
  }
}

static void writePpm(const char *name, const unsigned short *px, int w, int h) {
  FILE *f = fopen(name, "wb");
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) {
    const uint16_t v = (uint16_t)((px[i] << 8) | (px[i] >> 8));
    const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc),
                            (uint8_t)((v << 3) & 0xf8)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

// mismatching panel pixels vs MAME; dumps ours and MAME's when any differ
static int compare(const MameFrame &m) {
  static unsigned short ref[VISIBLE][WIDTH];
  int i = 0;
  for (uint32_t k = 0; k < m.rle_len; k += 2) {
    for (int n = 0; n < m.rle[k]; n++, i++) {
      ref[i / WIDTH][i % WIDTH] = rgb565(m.pal9[m.rle[k + 1]]);
    }
  }
  TEST_ASSERT_EQUAL(VISIBLE * WIDTH, i);

  int bad = 0;
  for (int y = 0; y < VISIBLE; y++) {
    for (int x = 0; x < WIDTH; x++) {
      bad += screen[TOP + y][x] != ref[y][x];
    }
  }
  if (bad) {
    const char *dir = getenv("TMPDIR");
    char name[512];
    snprintf(name, sizeof(name), "%s/tron_%u_ours.ppm", dir ? dir : "/tmp", m.frame);
    writePpm(name, screen[TOP], WIDTH, VISIBLE);
    snprintf(name, sizeof(name), "%s/tron_%u_mame.ppm", dir ? dir : "/tmp", m.frame);
    writePpm(name, ref[0], WIDTH, VISIBLE);
  }
  printf("frame %u: %d px differ\n", m.frame, bad);
  return bad;
}

static void check(int idx, int maxBad) {
  boot();
  render(MAME_FRAMES[idx]);
  TEST_ASSERT_LESS_OR_EQUAL_INT(maxBad, compare(MAME_FRAMES[idx]));
}

// one sprite, code 0x38 (Tron figure), all pens white: native X wraps at 256
static void renderSprite(uint8_t sx) {
  boot();
  machine->reset();
  uint8_t *sr = memory + TRON_MEM_SPRITES;
  sr[0] = 240 - 100;  // native y 100
  sr[1] = 0x38;
  sr[2] = sx;
  for (int i = 1; i < 64; i++) {  // pen 0 (blank tile 0) black
    machine->palette9[i] = 0x1ff;
  }
  machine->publish_video();
  machine->prepare_frame();
  for (int row = 0; row < HEIGHT / 8; row++) {
    machine->render_row(row);
    memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
  }
}

static void test_sprite_wraps_x(void) {
  static unsigned short ref[HEIGHT][WIDTH];
  renderSprite(4 + 14);  // native X 14-29
  memcpy(ref, screen, sizeof(ref));
  int refLit = 0;
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      refLit += ref[y][x] != 0;
    }
  }
  TEST_ASSERT_GREATER_THAN(20, refLit);
  renderSprite(4 - 2);   // native X 254-255, 0-13
  for (int i = 0; i < 16; i++) {
    const int x = (254 + i) & 0xff;
    TEST_ASSERT_EQUAL_MEMORY(ref[TOP + 14 + i], screen[TOP + x], sizeof(ref[0]));
  }
  int lit = 0;
  for (int y = TOP + 14; y < TOP + 254; y++) {
    for (int x = 0; x < WIDTH; x++) {
      lit += screen[y][x] != 0;
    }
  }
  TEST_ASSERT_EQUAL(0, lit);
}

static void test_title_300(void) { check(0, 0); }
static void test_figure_600(void) { check(1, 0); }
static void test_tower_1200(void) { check(2, 0); }
static void test_tanks_2100(void) { check(3, 0); }
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_title_300);
  RUN_TEST(test_figure_600);
  RUN_TEST(test_tower_1200);
  RUN_TEST(test_tanks_2100);
  RUN_TEST(test_sprite_wraps_x);
#endif
  return UNITY_END();
}
