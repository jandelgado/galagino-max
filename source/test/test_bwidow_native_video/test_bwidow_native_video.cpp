#include <stdio.h>
#include <string.h>
#include <unity.h>

// Black Widow machine at native HDMI geometry (bwidow_game_layout::
// SCREEN_NATIVE, 518 x 480, 60 strips, 1/16 beam units) in attract mode.
// Gravitar and Space Duel share bwidowMachine and differ in constants only
// (their layout tests). Needs the romconv headers, else skipped.
#define HAL_VIDEO_NATIVE 1
#if __has_include("../../src/machines/bwidow/bwidow_rom.h")
#define HAVE_ROM 1
#define GAME_BWIDOW
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/avg.cpp"
#include "../../src/emulation/dvg.cpp"
#define SK_RESET POKEY_SK_RESET // bwidowMachine.cpp has its own
#include "../../src/emulation/pokey.cpp"
#undef SK_RESET
#include "../../src/machines/bwidow/bwidow_layout.cpp"
#define private public
#define protected public
#include "../../src/machines/dvgMachine.cpp"
#include "../../src/machines/bwidow/bwidowMachine.cpp"
#include "../../src/machines/bwidow/bwidow.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}

static const int W = bwidow_game_layout::NATIVE_WIDTH, H = hal::VIDEO_NATIVE_H, STRIPS = H / 8;
static Input input;
static unsigned short frame_buffer[W * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint16_t image[H][W];

static void test_native_attract(void) {
  Arena::init();
  bwidow *machine = new bwidow();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
  TEST_ASSERT_TRUE(machine->native);
  TEST_ASSERT_EQUAL(W, machine->out_width);
  TEST_ASSERT_EQUAL(W, machine->renderWidth());
  TEST_ASSERT_EQUAL(480, machine->renderRows());

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

  // mapped lines of the last GO stay inside the picture
  const uint16_t n = machine->map_lines(machine->decode_frame(machine->memory + dvgMachine::VRAM_OFS));
  TEST_ASSERT_GREATER_THAN(10, n);
  for (uint16_t i = 0; i < n; i++) {
    const vec2d::Line &l = machine->work[i];
    TEST_ASSERT_TRUE(l.x0 >= 0 && l.x0 < W && l.x1 >= 0 && l.x1 < W);
    TEST_ASSERT_TRUE(l.y0 >= 0 && l.y0 < H && l.y1 >= 0 && l.y1 < H);
  }
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_native_attract);
#endif
  return UNITY_END();
}
