#include "../blob_data.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
#include "../../src/emulation/dc_block.h"

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
  static const uint8_t *m = blobData(robotron_rom_main_blob);
  static const uint8_t *s = blobData(robotron_rom_sound_blob);
  delete board;
  RobotronBoard *r = new RobotronBoard(ram, scanA, scanB, m, s);
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
  static const uint8_t *m = blobData(joust_rom_main_blob);
  static const uint8_t *s = blobData(joust_rom_sound_blob);
  delete board;
  JoustBoard *j = new JoustBoard(ram, scanA, scanB, m, s);
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

static SinistarBoard *bootSinistar(unsigned int actionKey = BOARD_KEY_EXTRA) {
  arena();
  static const uint8_t *m = blobData(sinistar_rom_main_blob);
  static const uint8_t *s = blobData(sinistar_rom_sound_blob);
  delete board;
  SinistarBoard *b = new SinistarBoard(ram, scanA, scanB, m, s, actionKey);
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
// Speech commands: the same command on a board with and one without the
// HC-55516 must differ somewhere (the 6808 clocks CVSD bits via CA2/CB2).
// One WilliamsSound at a time: the 6808 callbacks use a static instance.
static const int SPEECH_FRAMES = 120, SAMPLES_PER_FRAME = 400;
static int16_t speechRun[2][SPEECH_FRAMES * SAMPLES_PER_FRAME + 64];

static int runSound(const uint8_t *rom, uint8_t cmd, bool speech, int16_t *out) {
  static uint8_t ram256[256];
  static WilliamsSound snd;
  snd.attach(rom, 0xb000, ram256, speech);
  snd.reset();
  snd.run(16640);  // 6808 boot
  while (snd.sample() != WilliamsSound::EMPTY) {
  }
  snd.command(cmd);
  int n = 0;
  for (int f = 0; f < SPEECH_FRAMES; f++) {
    snd.run(16640);
    for (int v = snd.sample(); v != WilliamsSound::EMPTY; v = snd.sample()) {
      if (n < SPEECH_FRAMES * SAMPLES_PER_FRAME + 64) {
        out[n++] = (int16_t)v;
      }
    }
  }
  return n;
}

static int speechDelta(const uint8_t *rom, uint8_t cmd) {
  const int n = runSound(rom, cmd, true, speechRun[0]);
  TEST_ASSERT_EQUAL_INT(n, runSound(rom, cmd, false, speechRun[1]));
  int delta = 0;
  for (int i = 0; i < n; i++) {
    const int d = abs(speechRun[0][i] - speechRun[1][i]);
    delta = d > delta ? d : delta;
  }
  return delta;
}

// speech board mix at MAME's ratio (CVSD 0.8 : DAC 0.25) must stay in the
// renderer's range after williams.cpp's DAC_VOLUME 2 and the DC block, with
// speech peaks near full scale
void test_sinistar_mix_fits_range(void) {
  static const uint8_t *s = blobData(sinistar_rom_sound_blob);
  static const int DAC_VOLUME = 2, MIX_MAX = 511;  // williams.h, audio_mix.h
  int peak = 0;
  for (int cmd = 0; cmd < 32; cmd++) {
    const int n = runSound(s, (uint8_t)cmd, true, speechRun[0]);
    DcBlock dc;
    for (int i = 0; i < n; i++) {
      const int out = dc.filter(speechRun[0][i] * DAC_VOLUME);
      TEST_ASSERT_INT_WITHIN(MIX_MAX, 0, out);
      peak = abs(out) > peak ? abs(out) : peak;
    }
  }
  TEST_ASSERT_TRUE(peak > 400);
}

// Inputs scripted over frames after boot; returns the final frame hash
struct KeySpan {
  int from, to;
  unsigned int keys;
};

static uint32_t sinistarScript(unsigned int actionKey, const KeySpan *spans, int count, int frames) {
  SinistarBoard *b = bootSinistar(actionKey);
  while (!b->booted()) {
    b->runFrame();
  }
  for (int f = 0; f < frames; f++) {
    unsigned int k = 0;
    for (int i = 0; i < count; i++) {
      if (f >= spans[i].from && f < spans[i].to) {
        k |= spans[i].keys;
      }
    }
    b->setKeys(k);
    b->runFrame();
  }
  return frameHash();
}

// START as the action key (COIN pin / controller builds): START1 and BOMB
// rise together on the start press; the game must start as with START1
// alone (probe 2026-10-16: both start, only the enemy layout differs: the
// extra input bit shifts the random seed)
void test_sinistar_start_with_bomb(void) {
  const KeySpan start[] = { { 100, 105, BOARD_KEY_COIN }, { 160, 170, BOARD_KEY_START } };
  const uint32_t attract = sinistarScript(BOARD_KEY_EXTRA, start, 1, 600);
  const uint32_t plain = sinistarScript(BOARD_KEY_EXTRA, start, 2, 600);
  const uint32_t bomb = sinistarScript(BOARD_KEY_START, start, 2, 600);
  dump(".pio/sinistar_start_bomb.ppm");
  TEST_ASSERT_NOT_EQUAL(attract, plain);  // the start press did something
  TEST_ASSERT_NOT_EQUAL(attract, bomb);
}

// START1 held mid-game (EXTRA as action key sends START after a bomb):
// must not change the game
void test_sinistar_start1_mid_game_ignored(void) {
  const KeySpan play[] = { { 100, 105, BOARD_KEY_COIN }, { 160, 170, BOARD_KEY_START },
                           { 600, 660, BOARD_KEY_START } };
  const uint32_t without = sinistarScript(BOARD_KEY_EXTRA, play, 2, 900);
  const uint32_t with = sinistarScript(BOARD_KEY_EXTRA, play, 3, 900);
  TEST_ASSERT_EQUAL_HEX32(without, with);
}

void test_sinistar_speech_commands(void) {
  static const uint8_t *s = blobData(sinistar_rom_sound_blob);
  int speaking = 0, loudest = 0;
  for (int cmd = 0; cmd < 64; cmd++) {
    const int d = speechDelta(s, (uint8_t)cmd);
    if (d > 16) {
      speaking++;
    }
    loudest = d > loudest ? d : loudest;
  }
  // 0x02 0x0c 0x10 0x11 0x12 0x14 0x19 0x1d speak, b5 is ignored: 16 of 64
  // (probe 2026-10-16, deltas 148-154 with SPEECH_GAIN 192)
  TEST_ASSERT_EQUAL_INT(16, speaking);
  TEST_ASSERT_TRUE(loudest > 100);
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
  RUN_TEST(test_sinistar_speech_commands);
  RUN_TEST(test_sinistar_mix_fits_range);
  RUN_TEST(test_sinistar_start_with_bomb);
  RUN_TEST(test_sinistar_start1_mid_game_ignored);
#endif
  return UNITY_END();
}
