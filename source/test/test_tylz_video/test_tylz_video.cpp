#include <stdio.h>
#include <string.h>
#include <unity.h>

// Tylz renderer (gottlieb_video.h ROT0 path) against MAME snapshots: every
// panel line of 25 frames (title, attract, play) must match the snapshot
// line CRC (native x 4-243). Gfx from romconv (./convert.sh tylz); without
// the headers the MAME test is skipped.
#include "../../src/machines/gottlieb/gottlieb_video.h"
#if __has_include("../../src/machines/tylz/tylz_gfx.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/machines/tylz/tylz_gfx.h"
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

using namespace gottlieb_video;

static const int ROWS = 288 / 8;
static uint16_t fb[WIDTH * 8];

// pen k -> RGB565 value k + 1 (0 stays distinguishable from black)
static void testPens(uint16_t *pens) {
  for (int i = 0; i < PENS; i++) {
    pens[i] = (uint16_t)(i + 1);
  }
}

// 4 tiles: 0 blank, 1 all pen 2, 2 all pen 0, 3 unused; 1 sprite: all pen 5
static uint32_t tiles[4][8];
static uint32_t sprites[1][32];

static void fillGfx(void) {
  memset(tiles, 0, sizeof(tiles));
  for (int y = 0; y < 8; y++) {
    tiles[1][y] = 0x22222222;
  }
  for (int w = 0; w < 32; w++) {
    sprites[0][w] = 0x55555555;
  }
}

// sprite list entry at native (x, y), code 0
static void putSprite(uint8_t *spr, int x, int y) {
  memset(spr, 0xff, 0x100);   // other slots below the visible area
  spr[0] = (uint8_t)(y + SPRITE_DY);
  spr[1] = (uint8_t)(x + SPRITE_DX);
  spr[2] = 0xff;   // code ^ 0xff = 0
}

static void renderLine(int line, const uint8_t *vram, uint8_t ctrl, const uint16_t *pens, const Sprite *list,
                       int n) {
  renderStripRot0(line / 8, vram, ctrl, pens, tiles, sprites, list, n, fb);
}

static void test_sprite_clip_left(void) {
  fillGfx();
  uint16_t pens[PENS];
  testPens(pens);
  static uint8_t vram[0x400], spr[0x100];
  memset(vram, 1, sizeof(vram));   // tile 1 everywhere
  putSprite(spr, 2, 40);
  Sprite list[SPRITES];
  const int n = listSpritesRot0(spr, list);
  TEST_ASSERT_EQUAL_INT(1, n);

  const int line = ROT0_TOP + 40;
  renderLine(line, vram, 0, pens, list, n);
  const uint16_t *l = fb + (line & 7) * WIDTH - ROT0_LEFT;   // indexed by native x
  for (int x = ROT0_LEFT; x < SPRITE_CLIP_X; x++) {
    TEST_ASSERT_EQUAL_HEX16(pens[2], l[x]);
  }
  for (int x = SPRITE_CLIP_X; x < 2 + SPRITE_SIZE; x++) {
    TEST_ASSERT_EQUAL_HEX16(pens[5], l[x]);
  }
  TEST_ASSERT_EQUAL_HEX16(pens[2], l[2 + SPRITE_SIZE]);
}

static void test_sprite_right_edge(void) {
  fillGfx();
  uint16_t pens[PENS];
  testPens(pens);
  static uint8_t vram[0x400], spr[0x100];
  memset(vram, 0, sizeof(vram));
  putSprite(spr, WIDTH - 4, 100);   // native 236-251: 8 of 16 columns off the panel
  Sprite list[SPRITES];
  const int n = listSpritesRot0(spr, list);
  TEST_ASSERT_EQUAL_INT(1, n);

  static uint16_t guard[WIDTH * 9];
  memset(guard, 0, sizeof(guard));
  const int line = ROT0_TOP + 100;
  renderStripRot0(line / 8, vram, 0, pens, tiles, sprites, list, n, guard);
  const uint16_t *l = guard + (line & 7) * WIDTH;
  TEST_ASSERT_EQUAL_HEX16(pens[5], l[WIDTH - 1]);   // native 243
  TEST_ASSERT_EQUAL_HEX16(pens[5], l[WIDTH - 8]);   // native 236
  TEST_ASSERT_EQUAL_HEX16(pens[0], l[WIDTH - 9]);
  for (int i = WIDTH * 8; i < WIDTH * 9; i++) {
    TEST_ASSERT_EQUAL_HEX16(0, guard[i]);
  }

  putSprite(spr, ROT0_LEFT + WIDTH, 100);   // fully right of the panel
  TEST_ASSERT_EQUAL_INT(0, listSpritesRot0(spr, list));
  putSprite(spr, 20, 240);      // below the visible area
  TEST_ASSERT_EQUAL_INT(0, listSpritesRot0(spr, list));
}

static void test_bg_over_sprites(void) {
  fillGfx();
  uint16_t pens[PENS];
  testPens(pens);
  static uint8_t vram[0x400], spr[0x100];
  memset(vram, 2, sizeof(vram));   // tile 2: pen 0, transparent over sprites
  vram[(40 / 8) * MAP_W + 32 / 8] = 1;   // tile at native (32, 40): pen 2
  putSprite(spr, 24, 40);
  Sprite list[SPRITES];
  const int n = listSpritesRot0(spr, list);

  const int line = ROT0_TOP + 40;
  renderLine(line, vram, CTRL_BG_OVER_SPRITES, pens, list, n);
  const uint16_t *l = fb + (line & 7) * WIDTH - ROT0_LEFT;   // indexed by native x
  TEST_ASSERT_EQUAL_HEX16(pens[5], l[24]);   // sprite through pen 0
  TEST_ASSERT_EQUAL_HEX16(pens[2], l[32]);   // opaque tile pen hides it
  TEST_ASSERT_EQUAL_HEX16(pens[0], l[60]);   // no sprite: pen 0

  renderLine(line, vram, 0, pens, list, n);
  TEST_ASSERT_EQUAL_HEX16(pens[5], l[32]);   // sprites over tiles
}

static void test_border_lines_black(void) {
  fillGfx();
  uint16_t pens[PENS];
  testPens(pens);
  static uint8_t vram[0x400];
  memset(vram, 1, sizeof(vram));
  for (int row = 0; row < ROWS; row++) {
    renderStripRot0(row, vram, 0, pens, tiles, sprites, nullptr, 0, fb);
    for (int k = 0; k < 8; k++) {
      const int line = row * 8 + k;
      const bool border = line < ROT0_TOP || line >= ROT0_TOP + TILE_ROWS * 8;
      for (int x = 0; x < WIDTH; x++) {
        TEST_ASSERT_EQUAL_HEX16(border ? 0 : pens[2], fb[k * WIDTH + x]);
      }
    }
  }
}

#ifdef HAVE_ROM
static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = ~0u;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
  }
  return ~c;
}

static void test_render_matches_mame(void) {
  Arena::init();
  Asset<uint32_t[8], COMPRESSED> romTiles(tylz_tiles_blob);
  Asset<uint32_t[32], COMPRESSED> spr(tylz_sprites_blob);
  Asset<unsigned char, COMPRESSED> levels(tylz_levels_blob);
  uint16_t pens[PENS];
  Sprite list[SPRITES];

  const int frames = sizeof(MAME_FRAMES) / sizeof(MAME_FRAMES[0]);
  int bad = 0;
  for (int i = 0; i < frames; i++) {
    const MameFrame &m = MAME_FRAMES[i];
    buildPens(m.pal, levels.data(), pens);
    const int n = listSpritesRot0(m.spr, list);

    int frameBad = 0;
    for (int row = 0; row < ROWS; row++) {
      renderStripRot0(row, m.vram, m.ctrl, pens, romTiles.data(), spr.data(), list, n, fb);
      for (int k = 0; k < 8; k++) {
        const int line = row * 8 + k - ROT0_TOP;
        if (line < 0 || line >= TILE_ROWS * 8) {
          continue;
        }
        frameBad += crc32((const uint8_t *)(fb + k * WIDTH), WIDTH * 2) != m.line_crc[line];
      }
    }
    if (frameBad) {
      printf("frame %d (ctrl %02x): %d lines differ\n", m.frame, m.ctrl, frameBad);
      bad++;
    }
  }
  TEST_ASSERT_EQUAL_INT(0, bad);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_sprite_clip_left);
  RUN_TEST(test_sprite_right_edge);
  RUN_TEST(test_bg_over_sprites);
  RUN_TEST(test_border_lines_black);
#ifdef HAVE_ROM
  RUN_TEST(test_render_matches_mame);
#endif
  return UNITY_END();
}
