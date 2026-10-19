#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Choplifter video on the System 2 board (MAME sys2row,
// screen_update_system2_rowscroll): fg page 0, bg pages chosen by page 0
// bytes 740..746, x scroll per 8 lines, sprites 14 units right. MAME RAM
// states rendered through the machine class and compared with the MAME
// snapshot window x 8..247. Needs the romconv headers (./convert.sh
// choplift). Arduino.h / FastLED.h / esp_attr.h here are host stubs.
#if __has_include("../../src/machines/choplift/choplift_main_rom.h")
#define ENABLE_CHOPLIFT
#endif

#include "../../src/machines/system1/system1_collision.cpp"  // before Z80.h: its `word` macro
#ifdef ENABLE_CHOPLIFT
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/system1/system1.cpp"
#include "../../src/machines/choplift/choplift.cpp"
#undef private
#undef protected

struct MameFrame {
  uint32_t frame;
  uint8_t vram[0x1000];
  uint8_t pal[0x600];
  uint8_t spr[0x800];
  uint32_t crc[224];  // per line, window x 8..247
};

#include "mame_choplift_frames.h"
#endif

using namespace system1_video;

#ifdef ENABLE_CHOPLIFT
static system1 *machine;
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

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
void Reset6502(M6502 *) {}

static Input input;
static unsigned short frame_buffer[ROT0_WIDTH * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
  }
  return ~c;
}

static void bootChoplift() {
  delete machine;
  machine = nullptr;
  Arena::reserve(&choplift_arena_plan);
  machine = new choplift();
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// MAME RAM into the machine, latched as at vblank
static void load(const MameFrame &f) {
  memcpy(&memory[system1::VIDEO_RAM], f.vram, sizeof(f.vram));
  memcpy(&memory[system1::SPRITE_RAM], f.spr, sizeof(f.spr));
  machine->current_cpu = system1::MAIN_CPU;
  for (int i = 0; i < (int)sizeof(f.pal); i++) {
    machine->wrZ80(0xd800 + i, f.pal[i]);
  }
  machine->startVblank();
  machine->prepare_frame();
}

// env CHOPLIFT_DUMP=<dir>: rendered frames as <dir>/<frame>.ppm for a visual diff
static void dumpLine(FILE *ppm, const uint16_t *line) {
  for (int x = 0; x < ROT0_WIDTH; x++) {
    const uint16_t c = (uint16_t)((line[x] >> 8) | (line[x] << 8));
    const uint8_t rgb[3] = {(uint8_t)((c >> 8) & 0xf8), (uint8_t)((c >> 3) & 0xfc),
                            (uint8_t)((c << 3) & 0xf8)};
    fwrite(rgb, 1, 3, ppm);
  }
}

// renders all strips, returns the number of lines that differ from MAME
static int compare(const MameFrame &f) {
  int bad = 0;
  FILE *ppm = nullptr;
  if (getenv("CHOPLIFT_DUMP")) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%u.ppm", getenv("CHOPLIFT_DUMP"), f.frame);
    ppm = fopen(path, "wb");
    fprintf(ppm, "P6 %d %d 255\n", ROT0_WIDTH, VIS_LINES);
  }
  for (int strip = machine->firstStrip; strip <= machine->lastStrip; strip++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(strip);
    for (int l = 0; l < 8; l++) {
      const int y = strip * 8 + l - ROT0_FIRST_ROW;
      if (ppm) {
        dumpLine(ppm, &frame_buffer[l * ROT0_WIDTH]);
      }
      const uint32_t got = crc32((const uint8_t *)&frame_buffer[l * ROT0_WIDTH],
                                 ROT0_WIDTH * sizeof(uint16_t));
      if (got != f.crc[y]) {
        if (bad == 0) {
          printf("frame %u: first bad line %d\n", f.frame, y);
          fflush(stdout);
        }
        bad++;
      }
    }
  }
  if (ppm) {
    fclose(ppm);
  }
  return bad;
}

// title 2850 (one row scroll for all lines) and flight 3456, 3600, 4000
// (HUD, sky, mountains and a perspective ground ramp each with their own
// scroll, helicopter sprite). Not 3800: the game polls collision during
// the frame, MAME then draws the lines above the beam early
// (update_partial), and a HUD gauge tile written later in that frame is
// in the RAM dump but not in the snapshot.
static void test_render_matches_mame(void) {
  bootChoplift();
  int bad = 0;
  for (const MameFrame &f : MAME_CHOPLIFT) {
    load(f);
    TEST_ASSERT_EQUAL(8, machine->winX);
    bad += compare(f);
  }
  TEST_ASSERT_EQUAL_MESSAGE(0, bad, "choplift lines differ from MAME");
}
#endif

static uint32_t rnd(void) {
  static uint32_t s = 12345;
  s = s * 1103515245u + 12345u;
  return s >> 8;
}

static uint16_t identityPal[0x600];
static uint16_t MAP[64];  // identity tile map

// one transparent tile (code 0) and opaque tile 1 with pens 1..7, 1 on
// every row: bg pixel x shows pen 1 + (x & 7) % 7
static uint32_t twoTiles[2][8];
static const uint8_t PENS = 7;

// MAME video_update_common, System 2: one native line, bg from the 2x2
// page map with the row's scroll, fg at x
static void referenceRow(const LineSrc &f, const uint8_t *prom, int y, int x0, int n,
                         uint16_t *out) {
  const int scroll = f.rowScroll[(y >> 3) & 0x1f];
  const int by = (y + f.scrollY) & 0x1ff;
  for (int i = 0; i < n; i++) {
    const int x = x0 + i;
    const int bx = ((2 * x - scroll) / 2) & 0x1ff;
    const uint8_t *page = f.bgPages[(by >> 8) * 2 + (bx >> 8)];
    uint8_t fgColor, bgColor;
    const uint8_t fp = tilePixel(f.fgPage, f.tiles, x, y, fgColor);
    const uint8_t bp = tilePixel(page, f.tiles, bx & 0xff, by & 0xff, bgColor);
    const uint16_t spr = out[i];
    const uint8_t source = prom[mixIndex(spr != 0, fp, fgColor, bp, bgColor)] & MIX_SOURCE;
    if (source == 0) {
      out[i] = f.pal[spr];
    } else if (source == 1) {
      out[i] = f.pal[FG_PENS | tilePen(fgColor, fp)];
    } else {
      out[i] = f.pal[BG_PENS | tilePen(bgColor, bp)];
    }
  }
}

void setUp(void) {
  for (int i = 0; i < 0x600; i++) {
    identityPal[i] = i;
  }
  for (int i = 0; i < 64; i++) {
    MAP[i] = i;
  }
  for (int r = 0; r < 8; r++) {
    twoTiles[0][r] = 0;
    twoTiles[1][r] = 0;
    for (int c = 0; c < 8; c++) {
      twoTiles[1][r] |= (uint32_t)(1 + c % PENS) << (3 * (7 - c));
    }
  }
}
void tearDown(void) {}

// tile word of code 1 with color c (bits 5..10 of the word)
static void putTile(uint8_t *page, int col, int row, uint8_t color) {
  const uint16_t d = 1 | (color << 5);
  page[2 * (row * PAGE_COLS + col)] = d & 0xff;
  page[2 * (row * PAGE_COLS + col) + 1] = d >> 8;
}

// bg map x -> pen of the test pages: left page color = tile column, right
// page color = 32 + tile column
static uint16_t bgPen(int x) {
  const int bx = x & 0x1ff;
  const int color = (bx & 0xff) / 8 + 32 * (bx >> 8);
  return BG_PENS | tilePen(color, 1 + (bx & 7) % PENS);
}

// lines 31 (row 3) and 32 (row 4) of one bg tile row each show the map
// shifted by their own row scroll, across the left / right page seam
static void test_rowscroll_per_8_lines(void) {
  static uint8_t fgPage[PAGE_BYTES], left[PAGE_BYTES], right[PAGE_BYTES], prom[256];
  memset(fgPage, 0, sizeof(fgPage));
  for (int row = 0; row < PAGE_COLS; row++) {
    for (int col = 0; col < PAGE_COLS; col++) {
      putTile(left, col, row, col);
      putTile(right, col, row, 32 + col);
    }
  }
  memset(prom, 2, sizeof(prom));  // bg everywhere
  uint16_t sel[MIX_PRIORITIES];
  const bool clearShowsBg = buildMixSel(prom, sel);

  int16_t rows[SCROLL_ROWS] = {};
  rows[3] = 20;   // bg 10 px right
  rows[4] = -60;  // bg 30 px left
  const LineSrc f = {fgPage, {left, right, left, right}, {twoTiles, MAP, 1, TILES_BY_ROW},
                     identityPal, sel, clearShowsBg, rows, 0};

  uint16_t row3[ROT0_WIDTH] = {}, row4[ROT0_WIDTH] = {};
  renderRow(f, 31, 8, 8, ROT0_WIDTH, 0, row3);
  renderRow(f, 32, 8, 8, ROT0_WIDTH, 0, row4);
  for (int i = 0; i < ROT0_WIDTH; i++) {
    TEST_ASSERT_EQUAL_HEX16(bgPen(8 + i - 10), row3[i]);
    TEST_ASSERT_EQUAL_HEX16(bgPen(8 + i + 30), row4[i]);
  }
  TEST_ASSERT_EQUAL_HEX16(bgPen(256), row4[256 - 38]);  // right page from x 218
}

// random 2x2 pages, row scrolls (odd, negative), y scroll, PROM and
// sprite pens against the per pixel reference
static void test_bg_pages_random(void) {
  static uint32_t tiles[64][8];
  static uint8_t fgPage[PAGE_BYTES], bg[BG_PAGES][PAGE_BYTES], prom[256];
  static uint16_t pal[0x600];
  uint16_t sel[MIX_PRIORITIES];

  for (int round = 0; round < 400; round++) {
    for (int t = 0; t < 64; t++) {
      for (int c = 0; c < 8; c++) {
        tiles[t][c] = t < 32 ? 0 : rnd() & rnd() & 0xffffff;
      }
    }
    for (int i = 0; i < PAGE_BYTES; i++) {
      fgPage[i] = rnd();
      for (int p = 0; p < BG_PAGES; p++) {
        bg[p][i] = rnd();
      }
    }
    for (int i = 0; i < 256; i++) {
      prom[i] = rnd();
      if ((round & 1) && (i & 3) == 3) {
        prom[i] = (prom[i] & ~MIX_SOURCE) | 2;
      }
    }
    for (int i = 0; i < 0x600; i++) {
      pal[i] = rnd();
    }
    int16_t rows[SCROLL_ROWS];
    for (int r = 0; r < SCROLL_ROWS; r++) {
      rows[r] = (int)(rnd() & SYS2_ROW_SCROLL_MASK) + SYS2_ROW_SCROLL_BIAS;
    }
    const bool clearShowsBg = buildMixSel(prom, sel);
    const LineSrc f = {fgPage, {bg[0], bg[1], bg[2], bg[3]}, {tiles, MAP, 63, TILES_BY_ROW},
                       pal, sel, clearShowsBg, rows, (uint8_t)rnd()};
    const int y = rnd() % VIS_LINES;
    const bool full = round % 4 == 0;
    const int x0 = full ? 0 : rnd() % 17;
    const int n = full ? 256 : ROT0_WIDTH;
    const uint32_t sprCols = rnd() & rnd();

    uint16_t want[256] = {}, got[256] = {};
    for (int i = 0; i < n; i++) {
      if ((sprCols >> ((x0 + i) / TILE_SIZE)) & 1) {
        want[i] = got[i] = (rnd() & 1) ? rnd() % 0x200 : 0;
      }
    }
    referenceRow(f, prom, y, x0, n, want);
    renderRow(f, y, x0, x0, n, sprCols, got);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(want, got, n);
  }
}

// page 0 offsets of the System 2 registers as raw 9 bit scroll
static void putRowScroll(uint8_t *vram, int row, uint16_t raw) {
  vram[SYS2_ROW_SCROLL + 2 * row] = raw & 0xff;
  vram[SYS2_ROW_SCROLL + 2 * row + 1] = raw >> 8;
}

static void selectPages(uint8_t *vram, uint8_t page) {
  for (int p = 0; p < BG_PAGES; p++) {
    vram[SYS2_PAGE_SELECT + 2 * p] = page;
  }
}

// sprite 0 over a bg tile of the selected page 1 (fg = page 0 empty):
// row scroll and the sprite x offset 14 put them on top of each other
static void test_collision_uses_selected_page(void) {
  static uint8_t vram[2 * PAGE_BYTES], spriteRam[SPRITES * SPRITE_BYTES];
  static uint8_t spriteRom[SPRITE_BANK], prom[256];
  const uint16_t ROW_DATA = 0x0100;  // 4 opaque pixels, then end
  const uint8_t BG_OPAQUE = 0x02;    // mixIndex: spr opaque, fg transparent, bg opaque prio 0
  memset(vram, 0, sizeof(vram));
  memset(spriteRam, 0, sizeof(spriteRam));
  memset(prom, 0x0c, sizeof(prom));  // no collision anywhere
  prom[BG_OPAQUE] = 0x00;
  spriteRom[ROW_DATA] = 0x11;
  spriteRom[ROW_DATA + 1] = 0x11;
  spriteRom[ROW_DATA + 2] = 0xff;
  for (int n = 1; n < SPRITES; n++) {
    spriteRam[n * SPRITE_BYTES] = SPRITE_LIST_END;
  }

  // bg tile column 10 (map x 80..87) on row 2; scroll -12: screen x 74..81
  putTile(vram + PAGE_BYTES, 10, 2, 0);
  putRowScroll(vram, 2, 490);
  selectPages(vram, 1);

  // field 136 + 14 = 512 units 150..157 = screen x 75..78 on lines 16, 17;
  // without the offset x 68..71 misses the tile
  uint8_t *e = spriteRam;
  e[0] = 15;
  e[1] = 17;
  e[2] = 136;
  e[6] = ROW_DATA & 0xff;
  e[7] = ROW_DATA >> 8;

  const Collision::Frame f = {spriteRam, spriteRom, 1, vram, {twoTiles, MAP, 1, TILES_BY_ROW},
                              prom, 0, VISAREA_FULL, VIDEO_SYSTEM2};
  Collision col;
  col.reset();
  col.scan(f);
  TEST_ASSERT_EQUAL_HEX8(0xff, col.mixRead(0));

  // page 0 selected: the bg there is empty
  selectPages(vram, 0);
  col.reset();
  col.scan(f);
  TEST_ASSERT_FALSE(col.hit());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_rowscroll_per_8_lines);
  RUN_TEST(test_bg_pages_random);
  RUN_TEST(test_collision_uses_selected_page);
#ifdef ENABLE_CHOPLIFT
  Arena::init();
  RUN_TEST(test_render_matches_mame);
#endif
  return UNITY_END();
}
