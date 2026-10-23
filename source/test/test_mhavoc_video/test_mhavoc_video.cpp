#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Renders MAME mhavoc VRAM + color RAM dumps through the machine (decode,
// map without text enlargement, strip raster) and compares the lit pixels
// with the MAME snapshot of the same frame, reduced to a panel mask
// (make_fixture.py). Vector endpoints round differently, so pixels match
// within 2 rows/columns.
// MHAVOC_DUMP_DIR=<dir> writes frame_<n>.ppm (ours) and mask_<n>.ppm.
#if __has_include("../../src/machines/mhavoc/mhavoc_rom.h")
#define HAVE_ROM 1
#define GAME_MHAVOC
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/avg.cpp"
#include "../../src/emulation/dvg.cpp"
#include "../../src/emulation/pokey.cpp"
#include "../../src/machines/bwidow/bwidow_layout.cpp"
#define private public
#define protected public
#include "../../src/machines/dvgMachine.cpp"
#include "../../src/machines/mhavoc/mhavoc_layout.cpp"
#include "../../src/machines/mhavoc/mhavoc.cpp"
#undef private
#undef protected
#include "mame_mhavoc_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}

static const int W = 240, H = 288, TOP = 40, ROWS = 208;
static Input input;
static unsigned short frame_buffer[W * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint16_t image[H][W];
// MAME's geometry: the machine without its text enlargement
struct mhavoc_mame : mhavoc {
  uint16_t map_lines(uint16_t count) override {
    for (uint16_t i = 0; i < count; i++) {
      work[i].color &= ~avg::GLYPH;
    }
    return bwidow_layout::map(mhavoc_layout::SCREEN, work, count, 0);
  }
};

static mhavoc *machine;

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new mhavoc_mame();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void render(const MameFrame &f) {
  memcpy(memory + dvgMachine::VRAM_OFS, f.ram, 0x1000);
  memcpy(memory + mhavoc::COLOR_OFS, f.ram + 0x1000, 0x20);
  machine->go();
  machine->prepare_frame();
  for (int strip = 0; strip < H / 8; strip++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(strip);
    memcpy(image[strip * 8], frame_buffer, sizeof(frame_buffer));
  }
}

static bool masked(const MameFrame &f, int row, int col) {
  if (row < TOP || row >= TOP + ROWS || col < 0 || col >= W) {
    return false;
  }
  const int i = (row - TOP) * W + col;
  return f.mask[i / 8] & (1 << (i & 7));
}

static bool near(bool (*lit)(const MameFrame &, int, int), const MameFrame &f, int row, int col) {
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      if (lit(f, row + dy, col + dx)) {
        return true;
      }
    }
  }
  return false;
}

static bool ours(const MameFrame &, int row, int col) {
  return row >= 0 && row < H && col >= 0 && col < W && image[row][col];
}

static void dump(const MameFrame &f) {
  const char *dir = getenv("MHAVOC_DUMP_DIR");
  if (!dir) {
    return;
  }
  char path[512];
  snprintf(path, sizeof(path), "%s/frame_%u.ppm", dir, f.frame);
  FILE *o = fopen(path, "wb");
  snprintf(path, sizeof(path), "%s/mask_%u.ppm", dir, f.frame);
  FILE *m = fopen(path, "wb");
  fprintf(o, "P6 %d %d 255\n", W, H);
  fprintf(m, "P6 %d %d 255\n", W, H);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const uint16_t p = (image[y][x] >> 8) | (image[y][x] << 8); // byte swapped RGB565
      const uint8_t rgb[3] = {uint8_t((p >> 11) << 3), uint8_t(((p >> 5) & 0x3f) << 2), uint8_t((p & 0x1f) << 3)};
      fwrite(rgb, 1, 3, o);
      const uint8_t g = masked(f, y, x) ? 255 : 0, grey[3] = {g, g, g};
      fwrite(grey, 1, 3, m);
    }
  }
  fclose(o);
  fclose(m);
}

static void check(const MameFrame &f) {
  boot();
  render(f);
  dump(f);
  uint32_t lit = 0, hit = 0, ref = 0, found = 0;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      if (image[y][x]) {
        lit++;
        hit += near(masked, f, y, x);
      }
      if (masked(f, y, x)) {
        ref++;
        found += near(ours, f, y, x);
      }
    }
  }
  printf("frame %u: ours %u lit, %u near MAME; MAME %u lit, %u near ours\n", f.frame, lit, hit, ref, found);
  TEST_ASSERT_GREATER_THAN_UINT32(100, lit);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(lit * 9 / 10, hit);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(ref * 8 / 10, found);
}

static void test_space_wave(void) { check(mame_frames[0]); }
static void test_maze(void) { check(mame_frames[1]); }
static void test_title(void) { check(mame_frames[2]); }
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_space_wave);
  RUN_TEST(test_maze);
  RUN_TEST(test_title);
#endif
  return UNITY_END();
}
