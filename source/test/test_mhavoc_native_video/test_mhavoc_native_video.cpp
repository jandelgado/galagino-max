#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Major Havoc at native HDMI geometry (mhavoc_layout::SCREEN_NATIVE, 554 x
// 480, 60 strips, 1/16 beam units) on the MAME fixture frames of
// test_mhavoc_video. No native MAME mask exists: geometry is checked here,
// the picture against MAME by eye.
// MHAVOC_DUMP_DIR=<dir> writes native_<frame>.ppm.
#define HAL_VIDEO_NATIVE 1
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
#include "../test_mhavoc_video/mame_mhavoc_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}

static const int W = mhavoc_layout::NATIVE_WIDTH, H = hal::VIDEO_NATIVE_H, STRIPS = H / 8;
static Input input;
static unsigned short frame_buffer[W * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint16_t image[H][W];

static mhavoc *machine;

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new mhavoc();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void load(const MameFrame &f) {
  memcpy(memory + dvgMachine::VRAM_OFS, f.ram, 0x1000);
  memcpy(memory + mhavoc::COLOR_OFS, f.ram + 0x1000, 0x20);
}

static void render(void) {
  machine->go();
  machine->prepare_frame();
  for (int strip = 0; strip < STRIPS; strip++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(strip);
    memcpy(image[strip * 8], frame_buffer, sizeof(frame_buffer));
  }
}

static void dump(uint32_t frame) {
  const char *dir = getenv("MHAVOC_DUMP_DIR");
  if (!dir) {
    return;
  }
  char path[512];
  snprintf(path, sizeof(path), "%s/native_%05u.ppm", dir, (unsigned)frame);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6 %d %d 255\n", W, H);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const uint16_t p = (image[y][x] >> 8) | (image[y][x] << 8); // byte swapped RGB565
      const uint8_t rgb[3] = {uint8_t((p >> 11) << 3), uint8_t(((p >> 5) & 0x3f) << 2), uint8_t((p & 0x1f) << 3)};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

static void test_native_geometry(void) {
  boot();
  TEST_ASSERT_TRUE(machine->native);
  TEST_ASSERT_EQUAL(554, machine->out_width);
  TEST_ASSERT_EQUAL(554, machine->renderWidth());
  TEST_ASSERT_EQUAL(480, machine->renderRows());
  TEST_ASSERT_EQUAL(4, machine->avg_memory().frac_bits);
  TEST_ASSERT_TRUE(machine->avg_memory().sparkle_points);
}

// visarea corners land on the last column and row
static void test_native_corners(void) {
  const int16_t r = 300 << mhavoc_layout::NATIVE_FRAC, b = 260 << mhavoc_layout::NATIVE_FRAC;
  vec2d::Line l[2] = {};
  l[0].x0 = 0, l[0].y0 = 0, l[0].x1 = r, l[0].y1 = b, l[0].color = 1;
  l[1].x0 = -100, l[1].y0 = b + 100, l[1].x1 = r + 100, l[1].y1 = -100, l[1].color = 1;
  TEST_ASSERT_EQUAL(2, bwidow_layout::map(mhavoc_layout::SCREEN_NATIVE, l, 2, 0));
  TEST_ASSERT_EQUAL(0, l[0].x0);
  TEST_ASSERT_EQUAL(0, l[0].y0);
  TEST_ASSERT_EQUAL(W - 1, l[0].x1);
  TEST_ASSERT_EQUAL(H - 1, l[0].y1);
  for (const vec2d::Line &k : l) {
    TEST_ASSERT_TRUE(k.x0 >= 0 && k.x0 < W && k.x1 >= 0 && k.x1 < W);
    TEST_ASSERT_TRUE(k.y0 >= 0 && k.y0 < H && k.y1 >= 0 && k.y1 < H);
  }
}

static void test_native_frames_inside(void) {
  boot();
  for (const MameFrame &f : mame_frames) {
    load(f);
    const uint16_t n = machine->map_lines(machine->decode_frame(nullptr));
    TEST_ASSERT_GREATER_THAN(100, n);
    for (uint16_t i = 0; i < n; i++) {
      const vec2d::Line &l = machine->work[i];
      TEST_ASSERT_TRUE(l.x0 >= 0 && l.x0 < W && l.x1 >= 0 && l.x1 < W);
      TEST_ASSERT_TRUE(l.y0 >= 0 && l.y0 < H && l.y1 >= 0 && l.y1 < H);
      TEST_ASSERT_LESS_THAN(mhavoc_layout::PALETTE_SIZE, l.color);
    }

    render();
    uint32_t lit = 0;
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        lit += image[y][x] != 0;
      }
    }
    printf("frame %u: %u lines, %u lit\n", (unsigned)f.frame, (unsigned)n, (unsigned)lit);
    TEST_ASSERT_GREATER_THAN_UINT32(1000, lit);
    dump(f.frame);
  }
}

// 1/16 units are whole units with more precision, nothing else
static void test_frac_bits_whole_units(void) {
  boot();
  load(mame_frames[0]);
  static vec2d::Line whole[dvgMachine::MAX_LINES], frac[dvgMachine::MAX_LINES];
  avg::Memory mem = machine->avg_memory();
  const avg::State start = machine->avg_state;
  avg::State s = start;
  mem.frac_bits = 0;
  const uint16_t n0 = avg::decode(mem, s, whole, dvgMachine::MAX_LINES);
  s = start;
  mem.frac_bits = mhavoc_layout::NATIVE_FRAC;
  const uint16_t n4 = avg::decode(mem, s, frac, dvgMachine::MAX_LINES);
  TEST_ASSERT_GREATER_THAN(100, n0);
  TEST_ASSERT_EQUAL(n0, n4);
  for (uint16_t i = 0; i < n0; i++) {
    const vec2d::Line &a = whole[i], &b = frac[i];
    TEST_ASSERT_INT_WITHIN(1, a.x0, b.x0 >> 4);
    TEST_ASSERT_INT_WITHIN(1, a.y0, b.y0 >> 4);
    TEST_ASSERT_INT_WITHIN(1, a.x1, b.x1 >> 4);
    TEST_ASSERT_INT_WITHIN(1, a.y1, b.y1 >> 4);
    TEST_ASSERT_EQUAL(a.color, b.color);
  }
}

// sparkle split: segments per point add lines on frames with sparkle
static void test_sparkle_points_add_lines(void) {
  boot();
  static vec2d::Line lines[dvgMachine::MAX_LINES];
  uint32_t more = 0;
  for (const MameFrame &f : mame_frames) {
    load(f);
    avg::Memory mem = machine->avg_memory();
    avg::State s = machine->avg_state;
    mem.sparkle_points = false;
    const uint16_t one = avg::decode(mem, s, lines, dvgMachine::MAX_LINES);
    s = machine->avg_state;
    mem.sparkle_points = true;
    const uint16_t split = avg::decode(mem, s, lines, dvgMachine::MAX_LINES);
    TEST_ASSERT_GREATER_OR_EQUAL(one, split);
    more += split > one;
  }
  TEST_ASSERT_GREATER_THAN_UINT32(0, more);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_native_geometry);
  RUN_TEST(test_native_corners);
  RUN_TEST(test_native_frames_inside);
  RUN_TEST(test_frac_bits_whole_units);
  RUN_TEST(test_sparkle_points_add_lines);
#endif
  return UNITY_END();
}
