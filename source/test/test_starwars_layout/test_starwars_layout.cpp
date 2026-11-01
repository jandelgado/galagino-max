#include <unity.h>

// Star Wars native HDMI mapping: 1/16 beam units, VISIBLE (250 x 280)
// onto starwars NATIVE_WIDTH x 480 with square pixels. map_lines() is a
// member, so the machine is built: needs the romconv headers, else skipped.
#define HAL_VIDEO_NATIVE 1
#if __has_include("../../src/machines/starwars/starwars_rom.h")
#define HAVE_ROM 1
#define GAME_STARWARS
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/avg.cpp"
#include "../../src/emulation/pokey.cpp"
#include "../../src/emulation/tms5220.cpp"
#include "../../src/machines/starwars/starwars_math.cpp"
#define private public
#define protected public
#include "../../src/machines/starwars/starwars.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}

static const int W = 428, H = 480, F = 4;
static Input input;
static unsigned short frame_buffer[W * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static starwars *machine;

unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) {
  return machine->m6809_read_opcode(s, a);
}

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new starwars();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static vec2d::Line line(int x0, int y0, int x1, int y1, int color) {
  vec2d::Line l = {};
  l.x0 = x0;
  l.y0 = y0;
  l.x1 = x1;
  l.y1 = y1;
  l.color = color;
  return l;
}

static void assert_inside(const vec2d::Line &l) {
  TEST_ASSERT_TRUE(l.x0 >= 0 && l.x0 < W && l.x1 >= 0 && l.x1 < W);
  TEST_ASSERT_TRUE(l.y0 >= 0 && l.y0 < H && l.y1 >= 0 && l.y1 < H);
}

static void test_native_geometry(void) {
  boot();
  TEST_ASSERT_TRUE(machine->native);
  TEST_ASSERT_EQUAL(W, machine->out_width);
  TEST_ASSERT_EQUAL(W, machine->renderWidth());
  TEST_ASSERT_EQUAL(W * 2 * 8, machine->renderBuffer());
  TEST_ASSERT_EQUAL(H, machine->renderRows());
  TEST_ASSERT_EQUAL(H / 8, machine->strips());
}

// visible corners on the first and last column and row; lines crossing
// the edges are clipped inside
static void test_native_corners(void) {
  boot();
  const int r = 250 << F, b = 280 << F;
  machine->work[0] = line(0, 0, r, b, 0x1e);
  machine->work[1] = line(-100, b + 100, r + 100, -100, 0x2e);
  machine->work[2] = line(r, 0, 0, b, 0x3e);
  TEST_ASSERT_EQUAL(3, machine->map_lines(3));
  const vec2d::Line &l = machine->work[0];
  TEST_ASSERT_EQUAL(0, l.x0);
  TEST_ASSERT_EQUAL(0, l.y0);
  TEST_ASSERT_EQUAL(W - 1, l.x1);
  TEST_ASSERT_EQUAL(H - 1, l.y1);
  TEST_ASSERT_EQUAL(0x1e, l.color);
  TEST_ASSERT_EQUAL(W - 1, machine->work[2].x0);
  TEST_ASSERT_EQUAL(0, machine->work[2].x1);
  TEST_ASSERT_EQUAL(H - 1, machine->work[2].y1);
  for (int i = 0; i < 3; i++) {
    assert_inside(machine->work[i]);
  }
}

// beam center on the center, square pixels: equal beam steps, equal pixels
static void test_native_center(void) {
  boot();
  machine->work[0] = line(125 << F, 140 << F, (125 + 100) << F, (140 + 100) << F, 0x1e);
  TEST_ASSERT_EQUAL(1, machine->map_lines(1));
  const vec2d::Line &l = machine->work[0];
  TEST_ASSERT_INT_WITHIN(1, W / 2, l.x0);
  TEST_ASSERT_INT_WITHIN(1, H / 2, l.y0);
  TEST_ASSERT_INT_WITHIN(1, l.x1 - l.x0, l.y1 - l.y0);
}

// lines off the visible area are dropped
static void test_native_drop(void) {
  boot();
  machine->work[0] = line(-200, 10 << F, -100, 20 << F, 0x1e);
  machine->work[1] = line(10 << F, (280 << F) + 1, 20 << F, (290 << F), 0x1e);
  machine->work[2] = line(10 << F, 10 << F, 20 << F, 20 << F, 0x2e);
  TEST_ASSERT_EQUAL(1, machine->map_lines(3));
  TEST_ASSERT_EQUAL(0x2e, machine->work[0].color);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_native_geometry);
  RUN_TEST(test_native_corners);
  RUN_TEST(test_native_center);
  RUN_TEST(test_native_drop);
#endif
  return UNITY_END();
}
