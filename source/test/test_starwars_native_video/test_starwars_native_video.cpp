#include <stdio.h>
#include <string.h>
#include <unity.h>

// Star Wars machine at native HDMI geometry (428 x 480, 60 strips, 1/16
// beam units) in attract mode. Needs the romconv headers, else skipped.
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

static const int W = 428, H = hal::VIDEO_NATIVE_H, STRIPS = H / 8;
static Input input;
static unsigned short frame_buffer[W * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint16_t image[H][W];
static starwars *machine;

unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) {
  return machine->m6809_read_opcode(s, a);
}

static void test_native_attract(void) {
  Arena::init();
  machine = new starwars();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
  TEST_ASSERT_EQUAL(W, machine->renderWidth());
  TEST_ASSERT_EQUAL(H, machine->renderRows());

  uint32_t best = 0;
  int16_t xmax = 0, ymax = 0;
  for (int frame = 0; frame < 900; frame++) {
    machine->run_frame();
    if (frame % 20) {
      continue;
    }

    // published lines inside the picture
    machine->go_list->visit([&](const starwars::GoList &g) {
      for (uint16_t i = 0; i < g.count; i++) {
        const starwars::PackedLine &p = g.lines[i];
        TEST_ASSERT_TRUE(p.x0 < W && p.x1 < W && p.y0 < H && p.y1 < H);
        xmax = p.x0 > xmax ? p.x0 : xmax;
        xmax = p.x1 > xmax ? p.x1 : xmax;
        ymax = p.y0 > ymax ? p.y0 : ymax;
        ymax = p.y1 > ymax ? p.y1 : ymax;
      }
    });

    machine->prepare_frame();
    for (int strip = 0; strip < STRIPS; strip++) {
      memset(frame_buffer, 0, sizeof(frame_buffer));
      machine->render_row(strip);
      memcpy(image[strip * 8], frame_buffer, sizeof(frame_buffer));
    }
    uint32_t lit = 0;
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        lit += image[y][x] != 0;
      }
    }
    best = lit > best ? lit : best;
  }
  printf("native attract: %u lit pixels at best, max x %d y %d\n", (unsigned)best, xmax, ymax);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, best);
  // whole units would shrink the picture to a 1/16 corner
  TEST_ASSERT_GREATER_THAN(W / 2, xmax);
  TEST_ASSERT_GREATER_THAN(H / 2, ymax);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_native_attract);
#endif
  return UNITY_END();
}
