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
#include "../../src/cpus/m6803/m6803.c"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/machines/williams/williams_sound.cpp"
#include "../../src/machines/williams/williams_board.cpp"
#include "../../src/machines/williams/b1_board.cpp"
#include "../../src/machines/williams/robotron_board.h"

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

static void runAttract(const char *game) {
  while (!board->booted()) {
    board->runFrame();
  }
  uint32_t hashes[4];
  for (int i = 0; i < 4; i++) {
    for (int f = 0; f < ATTRACT_STEP; f++) {
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

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROBOTRON
  RUN_TEST(test_robotron_boot_reaches_attract);
#endif
  return UNITY_END();
}
