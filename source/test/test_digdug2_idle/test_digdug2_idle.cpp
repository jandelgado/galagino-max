#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots Dig Dug II on the host through the machine class and checks that the
// main and sub CPU wait loops ("LDA flag / BEQ") are skipped, not stepped.
// ROM headers come from romconv (./convert.sh digdug2); without them the
// test is skipped.
#if __has_include("../../src/machines/digdug2/digdug2_rom_main.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/emulation/namco_wsg.cpp"
// counts the instructions the machine asks the CPUs to run
static uint32_t stepped;
static int counted_step(m6809_state *s, char count) {
  stepped += count;
  return m6809_step(s, count);
}
#define m6809_step counted_step
#define private public
#define protected public
#include "../../src/machines/digdug2/digdug2.cpp"
#undef private
#undef protected
#undef m6809_step
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static digdug2 *machine;
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }

static const uint32_t ATTRACT_FRAMES = 1800, FRAMES = 600;
static const uint32_t STEPPED_PER_FRAME = 2 * 1600 * 4; // both CPUs, every slice

static uint32_t nonZero(const uint8_t *p, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += p[i] != 0;
  }
  return c;
}

// both CPUs spend most slices in their wait loops: skipped, under half of
// the slices are stepped. Attract still draws: video RAM set.
static void test_idle_loops_skipped(void) {
  Arena::init();
  machine = new digdug2();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
  for (uint32_t f = 0; f < ATTRACT_FRAMES; f++) {
    machine->run_frame();
  }
  stepped = 0;
  for (uint32_t f = 0; f < FRAMES; f++) {
    machine->run_frame();
  }
  const uint32_t vram = nonZero(memory, 0x0800);
  printf("instructions per frame %u of %u, vram %u\n", stepped / FRAMES, STEPPED_PER_FRAME, vram);
  TEST_ASSERT_LESS_THAN_UINT32(STEPPED_PER_FRAME / 2, stepped / FRAMES);
  TEST_ASSERT_GREATER_THAN_UINT32(100, vram);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_idle_loops_skipped);
#endif
  return UNITY_END();
}
