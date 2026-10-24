#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "../../src/machines/mario/mario_video.h"

// Renders a MAME frame (tile RAM, sprite list, registers dumped by
// romconv/dev/mario/mame_ref.lua) through the machine class and
// compares every native line with MAME's snapshot. ROM headers come from
// romconv (./convert.sh mario); without them the render test is skipped.
#if __has_include("../../src/machines/mario/mario_rom_main.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/cpus/i8048/i8048.c"
#define MARIO_Z80_TABLES_INCLUDED
#define private public
#include "../../src/machines/mario/mario.cpp"
#undef private
#include "mame_mario_frame.h"
#endif

using namespace mario_video;

void setUp(void) {}
void tearDown(void) {}

static void test_scale_drops_every_16th_column(void) {
  TEST_ASSERT_EQUAL_UINT8(0, scale_x(0));
  TEST_ASSERT_EQUAL_UINT8(14, scale_x(14));
  TEST_ASSERT_EQUAL_UINT8(16, scale_x(15));
  TEST_ASSERT_EQUAL_UINT8(254, scale_x(239));
}

static void test_crop_drops_8_each_side(void) {
  TEST_ASSERT_EQUAL_UINT8(8, crop_x(0));
  TEST_ASSERT_EQUAL_UINT8(247, crop_x(239));
}

static void test_pan_target_follows_player(void) {
  TEST_ASSERT_EQUAL_UINT8(0, pan_target(0));
  TEST_ASSERT_EQUAL_UINT8(0, pan_target(64));
  TEST_ASSERT_EQUAL_UINT8(8, pan_target(128));
  TEST_ASSERT_EQUAL_UINT8(16, pan_target(192));
  TEST_ASSERT_EQUAL_UINT8(16, pan_target(255));
}

static void test_pan_moves_one_pixel(void) {
  TEST_ASSERT_EQUAL_UINT8(1, pan_step(0, 16));
  TEST_ASSERT_EQUAL_UINT8(15, pan_step(16, 0));
  TEST_ASSERT_EQUAL_UINT8(5, pan_step(5, 5));
}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
static mario *machine;
extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opPrefixZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void i8048_port_write(i8048_state_S *, unsigned char, unsigned char) {}
unsigned char i8048_port_read(i8048_state_S *, unsigned char) { return 0; }
unsigned char i8048_rom_read(i8048_state_S *, unsigned short) { return 0; }
unsigned char i8048_xdm_read(i8048_state_S *, unsigned char) { return 0; }
void i8048_xdm_write(i8048_state_S *, unsigned char, unsigned char) {}

// zlib crc32, as the fixture generator
static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xffffffff;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
  }
  return ~c;
}

static Input input;
static unsigned short frame_buffer[mario_video::OUT_WIDTH * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static void test_render_matches_mame(void) {
  Arena::init();
  machine = new mario();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->vblank.publish([](mario::VideoState &v) {
    memcpy(v.tiles, mame_tiles, sizeof(v.tiles));
    memcpy(v.sprites, mame_sprites, sizeof(v.sprites));
    v.scroll = mame_scroll;
    v.latch = mame_latch;
    v.player1_x = mame_player1_x;
  });
  machine->prepare_frame();

  // all 256 native columns from the pen buffer
  int bad = 0;
  FILE *dump = getenv("MARIO_DUMP") ? fopen(getenv("MARIO_DUMP"), "wb") : nullptr;
  for (short row = mario::FIRST_ROW; row < mario::LAST_ROW; row++) {
    machine->render_row(row);
    for (uint8_t l = 0; l < 8; l++) {
      uint16_t line[256];
      for (int x = 0; x < 256; x++) {
        line[x] = machine->palette[machine->pens[l][x]];
      }
      const int y = 8 * (row - mario::FIRST_ROW) + l;
      if (dump) {
        fwrite(line, sizeof(line), 1, dump);
      }
      if (crc32((const uint8_t *)line, sizeof(line)) != mame_line_crc[y]) {
        if (!bad) {
          printf("first bad line %d\n", y);
        }
        bad++;
      }
    }
  }
  if (dump) {
    fclose(dump);
  }
  TEST_ASSERT_EQUAL_INT(0, bad);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_scale_drops_every_16th_column);
  RUN_TEST(test_crop_drops_8_each_side);
  RUN_TEST(test_pan_target_follows_player);
  RUN_TEST(test_pan_moves_one_pixel);
#ifdef HAVE_ROM
  RUN_TEST(test_render_matches_mame);
#endif
  return UNITY_END();
}
