#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Renders MAME dotron frames from their RAM and palette and compares with
// MAME's snapshot halved to the 240x240 panel view (even pixels). Tiles
// are doubled by the hardware and sprites sit at even positions, sampled by
// romconv per flip variant, so every pixel must match. ROM headers come
// from romconv (./convert.sh dotron); without them the tests are skipped.
#if __has_include("../../src/machines/dotron/dotron_rom.h")
#define HAVE_ROM 1
#define ENABLE_DOTRON
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#define MCR_Z80_TABLES_INCLUDED
#include "../../src/emulation/ay8910.cpp"
#include "../../src/machines/mcr/mcr_ssio.cpp"
#define private public
#define protected public
#include "../../src/machines/dotron/dotron.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int WIDTH = 240, HEIGHT = 288, TOP = 24, VISIBLE = 240;
static const int SPRITE_RAM = 0x200;
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static dotron *machine;

unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short) { return 0xff; }
void OutZ80(unsigned short, unsigned char) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new dotron();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

static void renderScreen(void) {
  machine->publish_video();
  machine->prepare_frame();
  for (int row = 0; row < HEIGHT / 8; row++) {
    machine->render_row(row);
    memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
  }
}

// render MAME's RAM through publish/prepare/render_row
static void render(const MameFrame &m) {
  machine->reset();
  memcpy(memory + DOTRON_MEM_SPRITES, m.ram, SPRITE_RAM);
  memcpy(memory + DOTRON_MEM_VIDEO, m.ram + SPRITE_RAM, 0x800);
  memcpy(machine->palette9, m.pal9, sizeof(m.pal9));
  renderScreen();
}

static void writePpm(const char *name, const unsigned short *px, int w, int h) {
  FILE *f = fopen(name, "wb");
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) {
    const uint16_t v = (uint16_t)((px[i] << 8) | (px[i] >> 8));
    const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc),
                            (uint8_t)((v << 3) & 0xf8)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

// panel pixels covered by a visible sprite (+1 px margin): there the 2:1
// downsample may differ from MAME's even-pixel sample
static void spriteMask(const MameFrame &m, bool mask[VISIBLE][WIDTH]) {
  memset(mask, 0, sizeof(bool) * VISIBLE * WIDTH);
  for (int offs = 0; offs < SPRITE_RAM; offs += 4) {
    const uint8_t *sr = m.ram + offs;
    const uint8_t code = (sr[2] + 256 * ((sr[1] >> 3) & 1)) % 128;
    if ((machine->blank_sprites[code >> 6] >> (code & 63)) & 1) {
      continue;
    }
    const uint8_t sx = sr[3] - 3, sy = 241 - sr[0];
    for (int j = -1; j <= 16; j++) {
      for (int k = -1; k <= 16; k++) {
        const int x = (sx + k) & 0xff, y = (sy + j) & 0xff;
        if (x >= 8 && x <= 247 && y < VISIBLE) {
          mask[y][247 - x] = true;
        }
      }
    }
  }
}

// mismatches vs MAME: total and outside sprites; dumps both on any
static void compare(const MameFrame &m, int *total, int *tiles) {
  static unsigned short ref[VISIBLE][WIDTH];
  static bool mask[VISIBLE][WIDTH];
  int i = 0;
  for (uint32_t k = 0; k < m.rle_len; k += 2) {
    for (int n = 0; n < m.rle[k]; n++, i++) {
      ref[i / WIDTH][i % WIDTH] = rgb565(m.pal9[m.rle[k + 1]]);
    }
  }
  TEST_ASSERT_EQUAL(VISIBLE * WIDTH, i);

  spriteMask(m, mask);
  *total = *tiles = 0;
  for (int y = 0; y < VISIBLE; y++) {
    for (int x = 0; x < WIDTH; x++) {
      const bool bad = screen[TOP + y][x] != ref[y][x];
      *total += bad;
      *tiles += bad && !mask[y][x];
    }
  }
  if (*total) {
    const char *dir = getenv("TMPDIR");
    char name[512];
    snprintf(name, sizeof(name), "%s/dotron_%u_ours.ppm", dir ? dir : "/tmp", m.frame);
    writePpm(name, screen[TOP], WIDTH, VISIBLE);
    snprintf(name, sizeof(name), "%s/dotron_%u_mame.ppm", dir ? dir : "/tmp", m.frame);
    writePpm(name, ref[0], WIDTH, VISIBLE);
  }
  printf("frame %u: %d px differ, %d outside sprites\n", m.frame, *total, *tiles);
}

static void check(int idx) {
  boot();
  render(MAME_FRAMES[idx]);
  int total, tiles;
  compare(MAME_FRAMES[idx], &total, &tiles);
  TEST_ASSERT_EQUAL_INT(0, tiles);
  TEST_ASSERT_EQUAL_INT(0, total);
}

static void test_frame_180(void) { check(0); }
static void test_frame_600(void) { check(1); }
static void test_frame_1000(void) { check(2); }
static void test_frame_1500(void) { check(3); }
static void test_frame_1800(void) { check(4); }
static void test_frame_3900(void) { check(5); }
static void test_frame_4200(void) { check(6); }

// sprite codes (romconv data): 95 fully opaque, 126 all pen 8
static const uint8_t CODE_OPAQUE = 95, CODE_PEN8 = 126;

static void setSprite(int n, uint8_t code, uint8_t x, uint8_t y) {
  uint8_t *sr = memory + DOTRON_MEM_SPRITES + n * 4;
  sr[0] = 241 - y;
  sr[1] = 0x03;  // color bits inverted: color 0
  sr[2] = code;
  sr[3] = x + 3;
}

// a clear screen with a white palette: tile 0 pen 0 stays black
static void clearScene(void) {
  boot();
  machine->reset();
  for (int i = 1; i < 64; i++) {
    machine->palette9[i] = 0x1ff;
  }
  for (int n = 0; n < 128; n++) {
    setSprite(n, CODE_PEN8, 0, 240);  // off screen: Y 240-255
  }
}

static int litPixels(void) {
  int lit = 0;
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      lit += screen[y][x] != 0;
    }
  }
  return lit;
}

// pen 8 of the top sprite hides the opaque sprite below, the tile shows
static void test_sprite_priority_pen8(void) {
  clearScene();
  setSprite(0, CODE_OPAQUE, 100, 100);
  renderScreen();
  TEST_ASSERT_GREATER_THAN(200, litPixels());

  setSprite(1, CODE_PEN8, 100, 100);  // higher offset = on top
  renderScreen();
  TEST_ASSERT_EQUAL(0, litPixels());

  setSprite(0, CODE_PEN8, 100, 100);  // swap: opaque on top
  setSprite(1, CODE_OPAQUE, 100, 100);
  renderScreen();
  TEST_ASSERT_GREATER_THAN(200, litPixels());
}

// native X 248-255, 0-7: all cropped, no wrap garbage
static void test_sprite_wrap_edge(void) {
  clearScene();
  setSprite(0, CODE_OPAQUE, 248, 100);
  renderScreen();
  TEST_ASSERT_EQUAL(0, litPixels());

  setSprite(0, CODE_OPAQUE, 240, 100);  // X 240-255: columns 0-7 only
  renderScreen();
  int outside = 0, inside = 0;
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      if (!screen[y][x]) {
        continue;
      }
      outside += x >= 8;
      inside += x < 8;
    }
  }
  TEST_ASSERT_EQUAL(0, outside);
  TEST_ASSERT_GREATER_THAN(20, inside);
}

// strips outside the 240 visible rows stay black even with a full palette
static void test_blank_strips(void) {
  boot();
  render(MAME_FRAMES[5]);
  for (int y = 0; y < HEIGHT; y++) {
    if (y >= TOP && y < TOP + VISIBLE) {
      continue;
    }
    for (int x = 0; x < WIDTH; x++) {
      TEST_ASSERT_EQUAL_HEX16(0, screen[y][x]);
    }
  }
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_frame_180);
  RUN_TEST(test_frame_600);
  RUN_TEST(test_frame_1000);
  RUN_TEST(test_frame_1500);
  RUN_TEST(test_frame_1800);
  RUN_TEST(test_frame_3900);
  RUN_TEST(test_frame_4200);
  RUN_TEST(test_sprite_priority_pen8);
  RUN_TEST(test_sprite_wrap_edge);
  RUN_TEST(test_blank_strips);
#endif
  return UNITY_END();
}
