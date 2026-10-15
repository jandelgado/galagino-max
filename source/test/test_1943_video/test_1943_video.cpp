#include <stdio.h>
#include <string.h>
#include <unity.h>

// Renders MAME 1943b frames from their video RAM and compares every panel
// line with MAME's snapshot. ROM headers come from romconv (./convert.sh
// 1943); without them the tests are skipped. The machine's video latch
// and render state are private: this test opens them up.
#if __has_include("../../src/machines/1943/1943_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ym2203fm.cpp"
#include "../../src/emulation/ay8910.cpp"
#include "../../src/emulation/ym2203.cpp"
#define private public
#define protected public
#include "../../src/machines/1943/1943.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
extern "C" {
unsigned char OpZ80_INL(unsigned short) { return 0; }
unsigned char OpPrefixZ80_INL(unsigned short) { return 0; }
unsigned char RdZ80(unsigned short) { return 0; }
void WrZ80(unsigned short, unsigned char) {}
unsigned char InZ80(unsigned short) { return 0xff; }
void OutZ80(unsigned short, unsigned char) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}

static const int WIDTH = 224, HEIGHT = 256, FIRST_ROW = 2;
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static _1943 *machine;

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t crc = ~0u;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new _1943();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

// main CPU writes as the game did, then the vblank latch and the render
static void load(const uint8_t *fg, const uint8_t *scroll, const uint8_t (*spr)[4], uint8_t c804,
                 uint8_t d806) {
  memcpy(memory + _1943::FG_CODE, fg, 0x800);
  for (int s = 0; s < 128; s++) {
    memcpy(memory + _1943::SPRITE_RAM + 32 * s, spr[s], 4);
  }
  for (int i = 0; i < 5; i++) {
    machine->wrZ80(0xd800 + i, scroll[i]);
  }
  machine->wrZ80(0xc804, c804);
  machine->wrZ80(0xd806, d806);
  machine->latchVideo();
  machine->prepare_frame();
}

static void render(void) {
  for (int row = FIRST_ROW; row < FIRST_ROW + HEIGHT / 8; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[8 * (row - FIRST_ROW)], frame_buffer, sizeof(frame_buffer));
  }
}

static void checkFrame(const MameFrame &m) {
  boot();
  load(m.fg, m.scroll, m.sprites, m.c804, m.d806);
  render();
  int bad = 0, first = -1;
  for (int y = 0; y < HEIGHT; y++) {
    if (crc32((const uint8_t *)screen[y], sizeof(screen[y])) != m.line_crc[y]) {
      bad++;
      first = first < 0 ? y : first;
    }
  }
  if (bad) {
    char name[64];
    snprintf(name, sizeof(name), "render_%u.raw", m.frame);
    FILE *f = fopen(name, "wb");
    fwrite(screen, sizeof(screen), 1, f);
    fclose(f);
    char msg[96];
    snprintf(msg, sizeof(msg), "frame %u: %d lines differ, first %d (dump %s)", m.frame, bad, first, name);
    TEST_FAIL_MESSAGE(msg);
  }
}

static void test_attract_title(void) { checkFrame(MAME_FRAMES[0]); }
static void test_attract_clouds(void) { checkFrame(MAME_FRAMES[1]); }
static void test_game_3000(void) { checkFrame(MAME_FRAMES[2]); }
static void test_game_4200(void) { checkFrame(MAME_FRAMES[3]); }
static void test_game_4800(void) { checkFrame(MAME_FRAMES[4]); }

// sx bit 8 (attr b4) set: sx = 0xF8 - 256 = -8, only native x 0..7 of
// the sprite shows, at the bottom panel lines; nothing else is drawn
static void test_negative_sx_sprite(void) {
  boot();
  static const uint8_t fg[0x800] = {};
  static const uint8_t scroll[5] = {};
  static uint8_t spr[128][4];
  memset(spr, 0, sizeof(spr));
  // sprite code with pixels in its first and last 8 native columns
  int code = -1;
  for (int c = 1; c < 2048 && code < 0; c++) {
    uint32_t lo = 0, hi = 0;
    for (int l = 0; l < 8; l++) {
      lo |= machine->sprite_gfx[c][2 * l] | machine->sprite_gfx[c][2 * l + 1];
      hi |= machine->sprite_gfx[c][2 * (l + 8)] | machine->sprite_gfx[c][2 * (l + 8) + 1];
    }
    code = lo && hi ? c : -1;
  }
  TEST_ASSERT_TRUE(code > 0);
  for (int s = 0; s < 128; s++) {
    spr[s][2] = 0xf0; // vy 224: below the visible area
  }
  spr[0][0] = code & 0xff;
  spr[0][1] = ((code >> 3) & 0xe0) | 0x10;
  spr[0][2] = 100 + 16; // vy 100
  spr[0][3] = 0xf8;
  load(fg, scroll, spr, 0x00, 0x40); // sprites only
  render();

  int inside = 0, outside = 0;
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      if (!screen[y][x]) {
        continue;
      }
      const bool in_box = y >= HEIGHT - 8 && x >= 100 && x < 116;
      inside += in_box;
      outside += !in_box;
    }
  }
  TEST_ASSERT_GREATER_THAN_INT(0, inside);
  TEST_ASSERT_EQUAL_INT(0, outside);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_negative_sx_sprite);
  RUN_TEST(test_attract_title);
  RUN_TEST(test_attract_clouds);
  RUN_TEST(test_game_3000);
  RUN_TEST(test_game_4200);
  RUN_TEST(test_game_4800);
#endif
  return UNITY_END();
}
