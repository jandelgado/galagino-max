#include <stdio.h>
#include <string.h>
#include <unity.h>
#if __has_include("../../src/machines/robotron/robotron_rom.h")
#define HAVE_ROBOTRON 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/asset.h"
#include "../../src/machines/robotron/robotron_rom.h"
#include "../../src/machines/williams/williams_palette.h"
#endif
#if __has_include("../../src/machines/joust/joust_rom.h") && defined(HAVE_ROBOTRON)
#define HAVE_JOUST 1
#include "../../src/machines/joust/joust_rom.h"
#endif
#if __has_include("../../src/machines/sinistar/sinistar_rom.h") && defined(HAVE_ROBOTRON)
#define HAVE_SINISTAR 1
#include "../../src/machines/sinistar/sinistar_rom.h"
#endif
#include "../../src/cpus/m6803/m6803.c"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/machines/williams/williams_sound.cpp"
#include "../../src/machines/williams/williams_board.cpp"
#include "../../src/machines/williams/b1_board.cpp"
#include "../../src/machines/williams/robotron_board.h"
#include "../../src/machines/williams/joust_board.h"
#include "../../src/machines/williams/sinistar_board.h"

// Boots the blitter board games (Robotron, Joust) through the board layer
// on the real ROMs (romconv headers, skipped without them); PPM frames to
// .pio/ for comparing with MAME. RAM and scanout are static: the Arena
// only holds the ROM assets of both games.
static WilliamsBoard *board = nullptr;
unsigned char m6809_read(m6809_state *, uint16_t a) { return board->read(a); }
unsigned char m6809_read_opcode(m6809_state *, uint16_t a) { return board->read(a); }
void m6809_write(m6809_state *, uint16_t a, uint8_t v) { board->write(a, v); }

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROBOTRON
static uint8_t cmos[1024], soundRam[256];
static uint8_t ram[WilliamsBoard::RAM_SIZE], scanA[Scanout::BUF_BYTES], scanB[Scanout::BUF_BYTES];
static const int NATIVE_WIDTH = 292;
static const int ATTRACT_STEP = 150;  // frames between hashes

static void arena() {
  static bool once = false;
  if (!once) {
    Arena::init();
    once = true;
  }
}

static void dump(const char *name) {
  const Scanout &s = board->scanout();
  const uint8_t f = s.front();
  static Asset<unsigned short, COMPRESSED> rgb(williams_rgb_blob);
  FILE *fp = fopen(name, "wb");
  if (!fp) {
    return;
  }
  fprintf(fp, "P6 %d %d 255\n", NATIVE_WIDTH, Scanout::ROWS);
  for (int y = 0; y < Scanout::ROWS; y++) {
    const uint8_t *row = s.frame(f) + y * Scanout::COLS;
    const uint8_t *pal = s.palette(f, y + Scanout::FIRST_LINE);
    for (int x = 0; x < NATIVE_WIDTH; x++) {
      uint16_t c = rgb.data()[pal[williamsPixel(row, x)]];
      c = (c >> 8) | (c << 8);  // stored byte swapped for SPI
      const uint8_t px[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2),
                              (uint8_t)((c & 31) << 3) };
      fwrite(px, 1, 3, fp);
    }
  }
  fclose(fp);
}

static uint32_t frameHash() {
  const Scanout &s = board->scanout();
  const uint8_t *f = s.frame(s.front());
  uint32_t h = 2166136261u;
  for (int i = 0; i < Scanout::BUF_BYTES; i++) {
    h = (h ^ f[i]) * 16777619u;
  }
  // Robotron's hall of fame only cycles colors
  for (int line = 0; line < Scanout::LINES; line += Scanout::BAND_LINES) {
    const uint8_t *p = s.palette(s.front(), line);
    for (int i = 0; i < 16; i++) {
      h = (h ^ p[i]) * 16777619u;
    }
  }
  return h;
}

static void runAttract(const char *game, int step = ATTRACT_STEP) {
  while (!board->booted()) {
    board->runFrame();
  }
  uint32_t hashes[4];
  for (int i = 0; i < 4; i++) {
    for (int f = 0; f < step; f++) {
      board->runFrame();
    }
    hashes[i] = frameHash();
    char name[64];
    snprintf(name, sizeof(name), ".pio/%s_%d.ppm", game, i);
    dump(name);
  }
  // attract animates
  TEST_ASSERT_NOT_EQUAL(hashes[0], hashes[1]);
  TEST_ASSERT_NOT_EQUAL(hashes[1], hashes[2]);
}

static RobotronBoard *bootRobotron() {
  arena();
  static Asset<unsigned char, COMPRESSED> m(robotron_rom_main_blob);
  static Asset<unsigned char, COMPRESSED> s(robotron_rom_sound_blob);
  delete board;
  RobotronBoard *r = new RobotronBoard(ram, scanA, scanB, m.data(), s.data());
  board = r;
  r->bindMemory(cmos, soundRam);
  r->reset();
  return r;
}

void test_robotron_boot_reaches_attract(void) {
  bootRobotron();
  runAttract("robotron");
}
#endif

#ifdef HAVE_JOUST
static JoustBoard *bootJoust() {
  arena();
  static Asset<unsigned char, COMPRESSED> m(joust_rom_main_blob);
  static Asset<unsigned char, COMPRESSED> s(joust_rom_sound_blob);
  delete board;
  JoustBoard *j = new JoustBoard(ram, scanA, scanB, m.data(), s.data());
  board = j;
  j->bindMemory(cmos, soundRam);
  j->reset();
  return j;
}

void test_joust_boot_reaches_attract(void) {
  bootJoust();
  runAttract("joust");
}

// CB2 high selects player 1 (INP1): left 0x01, flap 0x04; low = player 2
void test_joust_mux(void) {
  JoustBoard *j = bootJoust();
  while (!j->booted()) {
    j->runFrame();
  }
  j->setKeys(BOARD_KEY_LEFT | BOARD_KEY_FIRE);
  TEST_ASSERT_EQUAL_HEX8(0x05, j->in0ForTest(true) & 0x0f);
  TEST_ASSERT_EQUAL_HEX8(0x00, j->in0ForTest(false) & 0x0f);
}
#endif

#ifdef HAVE_SINISTAR
static uint8_t sram[4096];
// the high score table holds longer than ATTRACT_STEP
static const int SINISTAR_ATTRACT_STEP = 250;

static SinistarBoard *bootSinistar() {
  arena();
  static Asset<unsigned char, COMPRESSED> m(sinistar_rom_main_blob);
  static Asset<unsigned char, PLAIN> s(sinistar_rom_sound_blob);
  delete board;
  SinistarBoard *b = new SinistarBoard(ram, scanA, scanB, m.data(), s.data(), BOARD_KEY_EXTRA);
  board = b;
  b->bindMemory(cmos, soundRam);
  b->bindSram(sram);
  b->reset();
  return b;
}

void test_sinistar_boot_reaches_attract(void) {
  bootSinistar();
  runAttract("sinistar", SINISTAR_ATTRACT_STEP);
}

// 49-way stick at full deflection, MAME translate49 (williams_m.cpp:221)
void test_sinistar_stick49(void) {
  TEST_ASSERT_EQUAL_HEX8(0x77, SinistarBoard::stick49(0));
  TEST_ASSERT_EQUAL_HEX8(0x07, SinistarBoard::stick49(BOARD_KEY_LEFT));
  TEST_ASSERT_EQUAL_HEX8(0x87, SinistarBoard::stick49(BOARD_KEY_RIGHT));
  TEST_ASSERT_EQUAL_HEX8(0x78, SinistarBoard::stick49(BOARD_KEY_UP));
  TEST_ASSERT_EQUAL_HEX8(0x70, SinistarBoard::stick49(BOARD_KEY_DOWN));
  TEST_ASSERT_EQUAL_HEX8(0x08, SinistarBoard::stick49(BOARD_KEY_LEFT | BOARD_KEY_UP));
}

// window on: blits to 0x7400-0xbfff dropped, below and SRAM pass
void test_sinistar_blitter_window(void) {
  SinistarBoard *b = bootSinistar();
  b->write(0xc900, 0x04);  // window on, RAM visible
  b->write(0x7000, 0x00);
  b->write(0x7400, 0x00);
  b->write(0xd000, 0x00);
  // solid 1x1 fill of 0xaa at each target: control 0x10 (SOLID), size XOR 4
  const uint16_t dst[] = { 0x7000, 0x7400, 0xd000 };
  for (uint16_t d : dst) {
    b->write(0xca01, 0xaa);
    b->write(0xca04, d >> 8);
    b->write(0xca05, d & 0xff);
    b->write(0xca06, 1 ^ 4);
    b->write(0xca07, 1 ^ 4);
    b->write(0xca00, 0x10);
  }
  TEST_ASSERT_EQUAL_HEX8(0xaa, b->read(0x7000));
  TEST_ASSERT_EQUAL_HEX8(0x00, b->read(0x7400));
  TEST_ASSERT_EQUAL_HEX8(0xaa, b->read(0xd000));
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROBOTRON
  RUN_TEST(test_robotron_boot_reaches_attract);
#endif
#ifdef HAVE_JOUST
  RUN_TEST(test_joust_boot_reaches_attract);
  RUN_TEST(test_joust_mux);
#endif
#ifdef HAVE_SINISTAR
  RUN_TEST(test_sinistar_boot_reaches_attract);
  RUN_TEST(test_sinistar_stick49);
  RUN_TEST(test_sinistar_blitter_window);
#endif
  return UNITY_END();
}
