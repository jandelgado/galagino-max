#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots Joust, Robotron and Defender on the host through the board layer
// and checks that the main CPU wait loops are skipped, not stepped, without
// changing anything the game does: RAM, CPU, scanout and DAC samples hash
// to the values recorded with every instruction stepped. ROM headers come
// from romconv; without them the test is skipped.
#if __has_include("../../src/machines/joust/joust_rom.h") && \
    __has_include("../../src/machines/robotron/robotron_rom.h") && \
    __has_include("../../src/machines/defender/defender_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/asset.h"
#include "../../src/machines/joust/joust_rom.h"
#include "../../src/machines/robotron/robotron_rom.h"
#include "../../src/machines/defender/defender_rom.h"
#include "../../src/cpus/m6803/m6803.c"
#include "../../src/cpus/m6809/m6809.c"
// counts the main CPU instructions the board steps
static uint32_t stepped;
static int counted_step(m6809_state *s, char count) {
  stepped += count;
  return m6809_step(s, count);
}
#define m6809_step counted_step
#include "../../src/machines/williams/williams_board.cpp"
#undef m6809_step
#include "../../src/machines/williams/williams_sound.cpp"
#include "../../src/machines/williams/b1_board.cpp"
#include "../../src/machines/williams/defender_board.cpp"
#include "../../src/machines/williams/joust_board.h"
#include "../../src/machines/williams/robotron_board.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static WilliamsBoard *board = nullptr;
unsigned char m6809_read(m6809_state *, uint16_t a) { return board->read(a); }
unsigned char m6809_read_opcode(m6809_state *, uint16_t a) { return board->read(a); }
void m6809_write(m6809_state *, uint16_t a, uint8_t v) { board->write(a, v); }

static uint8_t cmos[1024], soundRam[256];
static uint8_t *ram, *scanA, *scanB;

// FNV-1a
static uint32_t hash(uint32_t h, const void *p, uint32_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (uint32_t i = 0; i < n; i++) {
    h = (h ^ b[i]) * 16777619u;
  }
  return h;
}

// every Arena reservation drops the previous game's assets
static void boardMemory(const ArenaPlan *plan) {
  Arena::reserve(plan);
  ram = Arena::alloc(WilliamsBoard::RAM_SIZE);
  scanA = Arena::alloc(Scanout::BUF_BYTES);
  scanB = Arena::alloc(Scanout::BUF_BYTES);
}

static const uint32_t FRAMES = 3000, WINDOW = 2100;  // attract incl. demo play

// Runs FRAMES frames from cold boot; returns the hash of what the game
// did, `perFrame` = main CPU instructions stepped per frame over the last
// WINDOW frames.
static uint32_t run(WilliamsBoard *b, uint32_t &perFrame) {
  board = b;
  b->bindMemory(cmos, soundRam);
  b->reset();
  uint32_t h = 2166136261u;
  for (uint32_t f = 0; f < FRAMES; f++) {
    if (f == FRAMES - WINDOW) {
      stepped = 0;
    }
    b->runFrame();
    for (int v = b->sound().sample(); v != WilliamsSound::EMPTY; v = b->sound().sample()) {
      h = hash(h, &v, sizeof(v));
    }
  }
  perFrame = stepped / WINDOW;

  const m6809_state *c = b->cpu();
  const uint16_t regs[] = { c->A, c->B, c->X, c->Y, c->U, c->S, c->PC, c->DP, c->CC };
  h = hash(h, regs, sizeof(regs));
  h = hash(h, ram, WilliamsBoard::RAM_SIZE);
  h = hash(h, cmos, sizeof(cmos));
  h = hash(h, soundRam, sizeof(soundRam));
  const Scanout &s = b->scanout();
  return hash(h, s.frame(s.front()), Scanout::BUF_BYTES);
}

static void check(const char *game, uint32_t h, uint32_t perFrame, uint32_t expectHash,
                  uint32_t maxPerFrame) {
  printf("%s: hash 0x%08x, %u instructions per frame\n", game, h, perFrame);
  TEST_ASSERT_EQUAL_HEX32(expectHash, h);
  TEST_ASSERT_LESS_THAN_UINT32(maxPerFrame, perFrame);
}

// TST ,X / BNE at 0xe0f0: 3656 instructions per frame stepped, 72 % of them the loop
void test_joust_wait_skipped(void) {
  boardMemory(&joust_arena_plan);
  joust_rom_main_asset *m = new joust_rom_main_asset(joust_rom_main_blob);
  joust_rom_sound_asset *s = new joust_rom_sound_asset(joust_rom_sound_blob);
  JoustBoard b(ram, scanA, scanB, m->data(), s->data());
  uint32_t perFrame;
  const uint32_t h = run(&b, perFrame);
  check("joust", h, perFrame, 0x27e9137b, 1200);
}

// LDA <$10 / CMPA #2 / BCS at 0xd19b: 4103 per frame, 80 % the loop
void test_robotron_wait_skipped(void) {
  boardMemory(&robotron_arena_plan);
  robotron_rom_main_asset *m = new robotron_rom_main_asset(robotron_rom_main_blob);
  robotron_rom_sound_asset *s = new robotron_rom_sound_asset(robotron_rom_sound_blob);
  RobotronBoard b(ram, scanA, scanB, m->data(), s->data());
  uint32_t perFrame;
  const uint32_t h = run(&b, perFrame);
  check("robotron", h, perFrame, 0xd327348c, 1200);
}

// LDA <$5d / BEQ at 0xe7c3: 4184 per frame, 68 % the loop
void test_defender_wait_skipped(void) {
  boardMemory(&defender_arena_plan);
  defender_rom_main_asset *m = new defender_rom_main_asset(defender_rom_main_blob);
  defender_rom_banked_asset *k = new defender_rom_banked_asset(defender_rom_banked_blob);
  defender_rom_sound_asset *s = new defender_rom_sound_asset(defender_rom_sound_blob);
  DefenderBoard b(ram, scanA, scanB, m->data(), k->data(), s->data(), BOARD_KEY_EXTRA);
  uint32_t perFrame;
  const uint32_t h = run(&b, perFrame);
  check("defender", h, perFrame, 0xfd617dc6, 1800);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  Arena::init();
  RUN_TEST(test_joust_wait_skipped);
  RUN_TEST(test_robotron_wait_skipped);
  RUN_TEST(test_defender_wait_skipped);
#endif
  return UNITY_END();
}
