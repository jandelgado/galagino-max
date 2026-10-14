#include <stdio.h>
#include <string.h>
#include <unity.h>
#if __has_include("../../src/machines/defender/defender_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/asset.h"
#include "../../src/machines/defender/defender_rom.h"
#include "../../src/machines/williams/williams_palette.h"
#endif
#include "../../src/cpus/m6803/m6803.c"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/machines/williams/williams_sound.cpp"
#include "../../src/machines/williams/williams_board.cpp"
#include "../../src/machines/williams/defender_board.cpp"

// Boots the real ROMs through the board layer (romconv headers, skipped
// without them) and dumps PPM frames to .pio/ for comparing with MAME.
static WilliamsBoard *board = nullptr;
unsigned char m6809_read(m6809_state *, uint16_t a) { return board->read(a); }
unsigned char m6809_read_opcode(m6809_state *, uint16_t a) { return board->read(a); }
void m6809_write(m6809_state *, uint16_t a, uint8_t v) { board->write(a, v); }

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static uint8_t cmos[1024], soundRam[256];
static const int NATIVE_WIDTH = 292;
static const int ATTRACT_STEP = 150;  // frames between hashes

// PPM of the front scanout buffer, 292 x 240
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
  return h;
}

// Arena filled once, in the machine's order (assets, RAM, scanout); every
// test builds a fresh board on the same buffers. Never Arena::reset() here:
// the static assets live in it.
static DefenderBoard *bootDefender() {
  static uint8_t *ram = nullptr, *a = nullptr, *b = nullptr;
  static const uint8_t *main = nullptr, *banked = nullptr, *snd = nullptr;
  if (!ram) {
    Arena::init();
    static Asset<unsigned char, COMPRESSED> m(defender_rom_main_blob);
    static Asset<unsigned char, COMPRESSED> k(defender_rom_banked_blob);
    static Asset<unsigned char, COMPRESSED> s(defender_rom_sound_blob);
    main = m.data();
    banked = k.data();
    snd = s.data();
    ram = Arena::alloc(WilliamsBoard::RAM_SIZE);
    a = Arena::alloc(Scanout::BUF_BYTES);
    b = Arena::alloc(Scanout::BUF_BYTES);
  }
  delete board;
  DefenderBoard *d = new DefenderBoard(ram, a, b, main, banked, snd, BOARD_KEY_EXTRA);
  board = d;
  d->bindMemory(cmos, soundRam);
  d->reset();
  return d;
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

void test_defender_boot_reaches_attract(void) {
  bootDefender();
  runAttract("defender");
}

void test_unmapped_bank_reads_zero(void) {
  bootDefender();
  board->write(0xd000, 0x0f);  // bank 15: unmapped
  TEST_ASSERT_EQUAL_HEX8(0x00, board->read(0xc123));
  board->write(0xd000, 0x01);  // bank 1 = banked ROM page 0
  TEST_ASSERT_NOT_EQUAL(0x00, board->read(0xc000) | board->read(0xc001) | board->read(0xc002));
}

void test_arena_fits_with_assets_first(void) {
  bootDefender();
  TEST_ASSERT_TRUE(Arena::bytesUsed() <= Arena::CAPACITY);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_defender_boot_reaches_attract);
  RUN_TEST(test_unmapped_bank_reads_zero);
  RUN_TEST(test_arena_fits_with_assets_first);
#endif
  return UNITY_END();
}
