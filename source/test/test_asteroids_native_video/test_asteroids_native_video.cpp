#include <stdio.h>
#include <string.h>
#include <unity.h>

// Asteroids machine at native HDMI geometry (NATIVE_WIDTH x 480, 60 strips,
// whole DVG units) in attract mode. Needs the romconv headers, else skipped.
#define HAL_VIDEO_NATIVE 1
#if __has_include("../../src/machines/asteroids/asteroids_rom.h")
#define HAVE_ROM 1
#define GAME_ASTEROIDS
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/dvg.cpp"
#include "../../src/emulation/discrete.cpp"
#include "../../src/machines/asteroids/asteroids_sound.cpp"
#define private public
#define protected public
#include "../../src/machines/dvgMachine.cpp"
#include "../../src/machines/asteroids/asteroids.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}

static const int W = NATIVE_WIDTH, H = hal::VIDEO_NATIVE_H, STRIPS = H / 8;
static Input input;
static unsigned short frame_buffer[W * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint16_t image[H][W];

static vec2d::Line line(int x0, int y0, int x1, int y1, int color) {
  vec2d::Line l = {};
  l.x0 = x0;
  l.y0 = y0;
  l.x1 = x1;
  l.y1 = y1;
  l.color = color;
  return l;
}

// one scale onto 480 rows: visible corners on the first and last column
// and row, the glyph tag goes
static void test_native_corners(void) {
  TEST_ASSERT_EQUAL(622, W);
  vec2d::Line l[2] = {line(0, 906, 1023, 118, 7 | dvg::GLYPH), line(-100, 1000, 1100, 0, 3)};
  TEST_ASSERT_EQUAL(2, map_native(l, 2));
  TEST_ASSERT_EQUAL(0, l[0].x0);
  TEST_ASSERT_EQUAL(0, l[0].y0);
  TEST_ASSERT_EQUAL(W - 1, l[0].x1);
  TEST_ASSERT_EQUAL(H - 1, l[0].y1);
  TEST_ASSERT_EQUAL(7, l[0].color);
  for (const vec2d::Line &k : l) {
    TEST_ASSERT_TRUE(k.x0 >= 0 && k.x0 < W && k.x1 >= 0 && k.x1 < W);
    TEST_ASSERT_TRUE(k.y0 >= 0 && k.y0 < H && k.y1 >= 0 && k.y1 < H);
  }
  vec2d::Line o[1] = {line(0, 100, 100, 50, 7)};
  TEST_ASSERT_EQUAL(0, map_native(o, 1));
}

static void test_native_attract(void) {
  Arena::init();
  asteroids *machine = new asteroids();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
  TEST_ASSERT_TRUE(machine->native);
  TEST_ASSERT_EQUAL(W, machine->out_width);
  TEST_ASSERT_EQUAL(W, machine->renderWidth());
  TEST_ASSERT_EQUAL(480, machine->renderRows());

  // copyright at its own size: LABS x=400, scale 0 as in the ROM
  const uint16_t labs_w1 = (0x853 - 0x800) * 2;
  TEST_ASSERT_EQUAL_HEX8(0x90, machine->vrom[labs_w1]);
  TEST_ASSERT_EQUAL_HEX8(0x01, machine->vrom[labs_w1 + 1]);

  uint32_t best = 0;
  for (int frame = 0; frame < 600; frame++) {
    machine->run_frame();
    if (frame % 20) {
      continue;
    }
    machine->prepare_frame();
    uint32_t lit = 0;
    for (int strip = 0; strip < STRIPS; strip++) {
      memset(frame_buffer, 0, sizeof(frame_buffer));
      machine->render_row(strip);
      memcpy(image[strip * 8], frame_buffer, sizeof(frame_buffer));
    }
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        lit += image[y][x] != 0;
      }
    }
    best = lit > best ? lit : best;
  }
  printf("native attract: %u lit pixels at best\n", (unsigned)best);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, best);

  // mapped lines of the last decode stay inside the picture
  const uint8_t *vram = machine->memory + dvgMachine::VRAM_OFS;
  const uint16_t n = machine->map_lines(machine->decode_frame(vram));
  TEST_ASSERT_GREATER_THAN(10, n);
  for (uint16_t i = 0; i < n; i++) {
    const vec2d::Line &l = machine->work[i];
    TEST_ASSERT_TRUE(l.x0 >= 0 && l.x0 < W && l.x1 >= 0 && l.x1 < W);
    TEST_ASSERT_TRUE(l.y0 >= 0 && l.y0 < H && l.y1 >= 0 && l.y1 < H);
    TEST_ASSERT_EQUAL(0, l.color & dvg::GLYPH);
  }
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_native_corners);
  RUN_TEST(test_native_attract);
#endif
  return UNITY_END();
}
