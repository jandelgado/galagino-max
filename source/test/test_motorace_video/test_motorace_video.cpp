#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Renders MAME motorace frames from their video RAM, sprite RAM and scroll
// and compares every panel line with MAME's snapshot (ROT270, cropped to
// x 16-255). ROM headers come from romconv (./convert.sh motorace); without
// them the test is skipped. MOTORACE_PPM=<dir> writes failing frames.
#define ENABLE_MOTORACE
#if __has_include("../../src/machines/motorace/motorace_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/cpus/m6803/m6803.c"
#include "../../src/emulation/ay8910.cpp"
#include "../../src/machines/motorace/motorace.cpp"
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

static const int WIDTH = 240, HEIGHT = 288, TOP = 24, LINES = 240;
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static motorace *machine;

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
  machine = new motorace();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

// main CPU writes as the game did, then the render as main.cpp drives it
static void render(const MameFrame &m) {
  for (int i = 0; i < 0x1000; i++) {
    machine->wrZ80(0x8000 + i, m.vram[i]);
  }
  for (int i = 0; i < 0x200; i++) {
    machine->wrZ80(0xc800 + i, m.spr[i]);
  }
  machine->wrZ80(0x9000, m.scroll & 0xff);
  machine->wrZ80(0xa000, m.scroll >> 8);

  machine->prepare_frame();
  for (int row = 0; row < HEIGHT / 8; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
  }
}

// tunnel tiles (colour 15) keep pens 6, 7 in front of sprites; no MAME
// fixture frame has a sprite under a tunnel
static int solidTile(void) {
  static Asset<uint32_t[8], COMPRESSED> tiles(motorace_tilemap_blob);
  int solid = -1;
  for (int t = 0; t < 1024 && solid < 0; t++) {
    bool front = true;
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        front = front && ((tiles[t][r] >> (c * 4)) & 7) >= 6;
      }
    }
    solid = front ? t : -1;
  }
  return solid;
}

static void renderTunnel(unsigned char color, bool withSprite, unsigned short *out) {
  static const int solid = solidTile();
  TEST_ASSERT_TRUE(solid >= 0);

  static MameFrame m;
  memset(&m, 0, sizeof(m));
  for (int i = 0; i < 0x800; i++) {
    m.vram[2 * i] = solid & 0xff;
    m.vram[2 * i + 1] = ((solid >> 8) << 6) | color;
  }
  if (withSprite) {
    m.spr[0] = 140;    // native y 100
    m.spr[2] = 0x50;   // car
    m.spr[3] = 100;    // native x 100
  }
  render(m);
  memcpy(out, screen, sizeof(screen));
}

static void test_tunnel_over_sprites(void) {
  boot();
  static unsigned short plain[HEIGHT][WIDTH], sprite[HEIGHT][WIDTH];
  renderTunnel(0x0f, false, plain[0]);
  renderTunnel(0x0f, true, sprite[0]);
  TEST_ASSERT_EQUAL_MEMORY(plain, sprite, sizeof(plain));

  renderTunnel(0x0e, false, plain[0]);
  renderTunnel(0x0e, true, sprite[0]);
  TEST_ASSERT_TRUE(memcmp(plain, sprite, sizeof(plain)) != 0);
}

static void writePpm(int frame) {
  const char *dir = getenv("MOTORACE_PPM");
  if (!dir) {
    return;
  }
  char path[512];
  snprintf(path, sizeof(path), "%s/motorace_%05d.ppm", dir, frame);
  FILE *f = fopen(path, "wb");
  if (!f) {
    return;
  }
  fprintf(f, "P6 %d %d 255\n", WIDTH, HEIGHT);
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      const uint16_t c = (uint16_t)((screen[y][x] >> 8) | (screen[y][x] << 8));   // unswap
      const uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2),
                              (uint8_t)((c & 31) << 3)};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

static void test_render_matches_mame(void) {
  boot();
  const int frames = sizeof(MAME_FRAMES) / sizeof(MAME_FRAMES[0]);
  int bad = 0;
  for (int i = 0; i < frames; i++) {
    const MameFrame &m = MAME_FRAMES[i];
    render(m);

    int frameBad = 0;
    for (int y = 0; y < HEIGHT; y++) {
      if (y < TOP || y >= TOP + LINES) {
        for (int x = 0; x < WIDTH; x++) {
          frameBad += screen[y][x] != 0;   // border lines stay black
        }
        continue;
      }
      frameBad += crc32((const uint8_t *)screen[y], WIDTH * 2) != m.line_crc[y - TOP];
    }
    if (frameBad) {
      printf("frame %d: %d lines differ\n", m.frame, frameBad);
      writePpm(m.frame);
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
  RUN_TEST(test_tunnel_over_sprites);
#endif
  return UNITY_END();
}
