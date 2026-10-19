#include <string.h>
#include <unity.h>

// Boots the real Gravitar ROM on the host and plays coin + START, to pin
// what the game reads from the controls (gravitar_inputs.h) and that the
// display list fits dvgMachine::MAX_LINES. The ROM headers are generated
// by romconv (./convert.sh gravitar) and not in the repo: without them
// the tests are skipped.
#if __has_include("../../src/machines/gravitar/gravitar_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/asset.h"
#include "../../src/machines/gravitar/gravitar_rom.h"
#include "../../src/machines/gravitar/gravitar_vrom.h"
#endif
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/avg.cpp"

// input.h needs Arduino.h; same values as there
#define BUTTON_LEFT 0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP 0x04
#define BUTTON_DOWN 0x08
#define BUTTON_FIRE 0x10
#define BUTTON_START 0x20
#define BUTTON_A 0x100
#define BUTTON_B 0x200
#include "../../src/machines/gravitar/gravitar_inputs.h"
#include "../../src/machines/gravitar/gravitar_dipswitches.h"

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const uint32_t CYCLES_PER_FRAME = 25200, CYCLES_PER_IRQ = 6144;
static const uint16_t MAX_LINES = 1024; // dvgMachine::MAX_LINES

// game RAM, found by tracing the ROM: the input byte read at 0xcc9a is
// inverted into 0x1d; rotation (0x9724) steps 0x27; thrust (0x959c) and
// shield/tractor (0x98d8) add 6 and 0x20 per tick to the fuel used in 0x48,
// which 0x9683 subtracts from player 1's BCD fuel at 0x16c:0x16a:0x168;
// fire (0xc43f, edge triggered) bumps 0x41.
static const uint16_t RAM_ANGLE = 0x27, RAM_SHOTS = 0x41;
static const uint16_t RAM_FUEL_LO = 0x168, RAM_FUEL_MID = 0x16a, RAM_FUEL_HI = 0x16c;

struct Board {
  uint8_t ram[0x800], vram[0x800];
  const uint8_t *rom, *vrom;
  uint32_t cycles;
  bool coin;
  unsigned int keys;
  uint16_t max_lines;
};

static Board board;
static m6502_t cpu;
static uint32_t next_irq;
static avg::State avg_state;
static vec2d::Line lines[MAX_LINES];

static void go(void) {
  const avg::Memory m = {board.vram, 0x0800, board.vrom, 0x0800, 0x3800, 0, 0, avg::COLOR, nullptr};
  const uint16_t n = avg::decode(m, avg_state, lines, MAX_LINES);
  if (n > board.max_lines) {
    board.max_lines = n;
  }
}

static uint8_t rd(m6502_t *, uint16_t a) {
  if (a >= 0xf000) {
    return board.rom[0x5000 + (a & 0x0fff)]; // 306.m1 again
  }
  if (a >= 0x9000) {
    return board.rom[a - 0x9000];
  }
  if (a < 0x0800) {
    return board.ram[a];
  }
  if (a >= 0x2000 && a < 0x2800) {
    return board.vram[a - 0x2000];
  }
  if (a >= 0x2800 && a < 0x6000) {
    return board.vrom[a - 0x2800];
  }
  if ((a & 0xf00f) == 0x6008) { // POKEY ALLPOT
    return (a & 0x0800) ? GRAVITAR_DSW1 : GRAVITAR_DSW0;
  }
  switch (a) {
    case 0x7000: return 0xff; // erased EAROM
    case 0x7800: return (board.coin ? 0x7d : 0x7f) | ((board.cycles & 0x100) ? 0x80 : 0);
    case 0x8000: return gravitar_in3(board.keys);
    case 0x8800: return gravitar_in4(board.keys);
  }
  return 0;
}

static void wr(m6502_t *c, uint16_t a, uint8_t v) {
  if (a < 0x0800) {
    board.ram[a] = v;
  } else if (a >= 0x2000 && a < 0x2800) {
    board.vram[a - 0x2000] = v;
  } else if (a == 0x8840) {
    go();
  } else if (a == 0x88c0) {
    c->irq = 0;
  }
}

static uint32_t bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0f); }

static uint32_t fuel(void) {
  return bcd(board.ram[RAM_FUEL_HI]) * 10000 + bcd(board.ram[RAM_FUEL_MID]) * 100 + bcd(board.ram[RAM_FUEL_LO]);
}

static void boot(void) {
  static bool arena = false;
  if (!arena) {
    Arena::init();
    arena = true;
  }
  static Asset<unsigned char, COMPRESSED> rom(gravitar_rom_blob), vrom(gravitar_vrom_blob);
  memset(&board, 0, sizeof(board));
  board.rom = rom.data();
  board.vrom = vrom.data();
  avg_state.reset(210 << 16, 200 << 16);
  memset(&cpu, 0, sizeof(cpu));
  cpu.read = rd;
  cpu.write = wr;
  cpu.user = &board;
  m6502_reset(&cpu);
  next_irq = CYCLES_PER_IRQ;
}

static void frames(uint32_t n) {
  const uint32_t end = board.cycles + n * CYCLES_PER_FRAME;
  while ((int32_t)(board.cycles - end) < 0) {
    if ((int32_t)(board.cycles - next_irq) >= 0) {
      cpu.irq = 1;
      next_irq += CYCLES_PER_IRQ;
    }
    board.cycles += m6502_step(&cpu);
  }
}

static void hold(unsigned int keys, uint32_t n) {
  board.keys = keys;
  frames(n);
  board.keys = 0;
}

// galagino's virtual coin: coin 100 ms, 500 ms pause, START held 100 ms+
static void coin_and_start(void) {
  board.coin = true;
  frames(6);
  board.coin = false;
  frames(30);
  hold(BUTTON_START, 10);
  frames(600);
}
#define NEED_ROM()
#else
#define NEED_ROM() TEST_IGNORE_MESSAGE("gravitar ROM headers missing, run romconv")
#endif

// fuel burnt while holding keys for 60 frames in a fresh game
static uint32_t burn(unsigned int keys) {
  boot();
  frames(600);
  coin_and_start();
  const uint32_t before = fuel();
  hold(keys, 60);
  return before - fuel();
}

// attract mode ignores thrust; one coin + START starts a game that uses it
static void test_start_starts_game(void) {
  NEED_ROM();
#ifdef HAVE_ROM
  boot();
  frames(600);
  const uint32_t attract = fuel();
  hold(BUTTON_UP, 60);
  TEST_ASSERT_EQUAL(attract, fuel());
  coin_and_start();
  const uint32_t game = fuel();
  TEST_ASSERT_NOT_EQUAL(0, game);
  hold(BUTTON_UP, 60);
  TEST_ASSERT_LESS_THAN(game, fuel());
#endif
}

static void test_up_thrusts(void) {
  NEED_ROM();
#ifdef HAVE_ROM
  TEST_ASSERT_EQUAL(0, burn(0));
  TEST_ASSERT_GREATER_THAN(0, burn(BUTTON_UP));
#endif
}

// shield/tractor burns fuel faster than thrust (0x20 vs 6 per tick)
static void test_down_and_b_shield(void) {
  NEED_ROM();
#ifdef HAVE_ROM
  const uint32_t thrust = burn(BUTTON_UP);
  TEST_ASSERT_GREATER_THAN(3 * thrust, burn(BUTTON_DOWN));
  TEST_ASSERT_GREATER_THAN(3 * thrust, burn(BUTTON_B));
#endif
}

static void test_fire_and_a_fire(void) {
  NEED_ROM();
#ifdef HAVE_ROM
  const unsigned int keys[] = {BUTTON_FIRE, BUTTON_A};
  for (unsigned int k : keys) {
    boot();
    frames(600);
    coin_and_start();
    const uint8_t shots = board.ram[RAM_SHOTS];
    const uint32_t before = fuel();
    hold(k, 5);
    TEST_ASSERT_NOT_EQUAL(shots, board.ram[RAM_SHOTS]);
    TEST_ASSERT_EQUAL(before, fuel());
  }
#endif
}

static void test_right_rotates(void) {
  NEED_ROM();
#ifdef HAVE_ROM
  boot();
  frames(600);
  coin_and_start();
  const uint8_t angle = board.ram[RAM_ANGLE];
  hold(BUTTON_RIGHT, 60);
  TEST_ASSERT_NOT_EQUAL(angle, board.ram[RAM_ANGLE]);
#endif
}

// attract mode, then a game with the ship thrusting and firing
static void test_line_count_fits(void) {
  NEED_ROM();
#ifdef HAVE_ROM
  boot();
  frames(3000);
  coin_and_start();
  for (int i = 0; i < 20; i++) {
    hold(BUTTON_UP | BUTTON_FIRE | ((i & 1) ? BUTTON_LEFT : 0), 30);
    frames(30);
  }
  TEST_ASSERT_GREATER_THAN(0, board.max_lines);
  TEST_ASSERT_LESS_THAN(MAX_LINES, board.max_lines);
#endif
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_start_starts_game);
  RUN_TEST(test_up_thrusts);
  RUN_TEST(test_down_and_b_shield);
  RUN_TEST(test_fire_and_a_fire);
  RUN_TEST(test_right_rotates);
  RUN_TEST(test_line_count_fits);
  return UNITY_END();
}
