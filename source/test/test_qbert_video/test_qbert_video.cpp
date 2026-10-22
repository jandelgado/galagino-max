#include <stdio.h>
#include <string.h>
#include <unity.h>

// Q*bert renderer (qbert_video.h) against MAME snapshots: every panel line
// of 35 frames (title, play, background over sprites) must match the
// snapshot line CRC. Gfx from romconv (./convert.sh qbert); without the
// headers the test is skipped.
#if __has_include("../../src/machines/qbert/qbert_gfx.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/machines/qbert/qbert_gfx.h"
#include "../../src/machines/qbert/qbert_video.h"
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
using namespace qbert_video;

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
  Asset<uint32_t[8], COMPRESSED> tiles(qbert_tiles_blob);
  Asset<uint32_t[32], COMPRESSED> spr(qbert_sprites_blob);
  Asset<unsigned char, COMPRESSED> levels(qbert_levels_blob);
  static uint16_t fb[WIDTH * 8];
  static VideoState v;
  uint16_t pens[PENS];
  Sprite list[SPRITES];

  const int frames = sizeof(MAME_FRAMES) / sizeof(MAME_FRAMES[0]);
  int bad = 0;
  for (int i = 0; i < frames; i++) {
    const MameFrame &m = MAME_FRAMES[i];
    memcpy(v.vram, m.vram, sizeof(v.vram));
    memcpy(v.spr, m.spr, sizeof(v.spr));
    memcpy(v.pal, m.pal, sizeof(v.pal));
    v.ctrl = m.ctrl;
    buildPens(v.pal, levels.data(), pens);
    const int n = listSprites(v.spr, list);

    int frameBad = 0;
    for (int row = TOP / 8; row < (TOP + NATIVE_W) / 8; row++) {
      renderStrip(row, v, pens, tiles.data(), spr.data(), list, n, fb);
      for (int k = 0; k < 8; k++) {
        const int line = row * 8 + k - TOP;
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
#ifdef HAVE_ROM
  RUN_TEST(test_render_matches_mame);
#endif
  return UNITY_END();
}
