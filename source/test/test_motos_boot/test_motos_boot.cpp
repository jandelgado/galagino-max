#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots the real Motos ROM on the host through the machine class: both
// 6809s, the two 56XX I/O chips (boot check, coins), WSG. ROM headers come
// from romconv (./convert.sh motos); without them the test is skipped.
#if __has_include("../../src/machines/motos/motos_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_MOTOS
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/emulation/namco_wsg.cpp"
#include "../../src/machines/digdug2/digdug2.cpp"
#include "../../src/machines/motos/motos.cpp"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
extern "C" void ResetZ80(Z80 *) {}

static Input input;
static unsigned short frame_buffer[224 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static motos *machine;
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }

// credit counter in main RAM (MAME motos: 0 -> 1 on a coin, 0 after start)
static const uint16_t CREDITS = 0x1807;
static const uint32_t COIN_FRAME = 1000, START_FRAME = 1200;
static uint32_t frame;

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new motos();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void runTo(uint32_t end) {
  while (frame < end) {
    keys = 0;
    if (frame >= COIN_FRAME && frame < COIN_FRAME + 5) {
      keys = BUTTON_COIN;
    }
    if (frame >= START_FRAME && frame < START_FRAME + 5) {
      keys = BUTTON_START;
    }
    machine->run_frame();
    frame++;
  }
  keys = 0;
}

// attract: the 56XX boot check passes and the game draws its tiles
static void test_boots_to_attract(void) {
  boot();
  runTo(900);
  uint32_t lit = 0;
  for (int a = 0; a < 0x800; a++) {
    lit += memory[a] != memory[0];
  }
  printf("tile RAM bytes unlike the first: %u\n", lit);
  TEST_ASSERT_GREATER_THAN_UINT32(50, lit);
}

// coins through the 56XX chip 0, then start uses the credit
static void test_coin_and_start(void) {
  boot();
  runTo(COIN_FRAME - 10);
  TEST_ASSERT_EQUAL_UINT8(0, memory[CREDITS]);
  runTo(COIN_FRAME + 100);
  TEST_ASSERT_EQUAL_UINT8(1, memory[CREDITS]);
  runTo(START_FRAME + 500);
  TEST_ASSERT_EQUAL_UINT8(0, memory[CREDITS]);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boots_to_attract);
  RUN_TEST(test_coin_and_start);
#endif
  return UNITY_END();
}
