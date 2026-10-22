#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Renders MAME frames (mame_frames.h, romconv/dev/contra) through the
// machine's render path and compares each of the 280 panel lines with the
// MAME snapshot by CRC32. On a mismatch the frame is written to
// $TMPDIR/contra_<frame>.ppm.
#if __has_include("../../src/machines/contra/contra_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_CONTRA
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/sid.cpp"
#include "../../src/emulation/sidplayer.cpp"
#define private public
#define protected public
#include "../../src/machines/contra/contra.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}
void Reset6502(M6502 *) {}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return 0; }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) {}
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return 0; }

static const int PANEL_LINES = 288, HEIGHT = 280;   // WIDTH, TOP from contra.cpp

static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[PANEL_LINES][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static contra *machine;

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xffffffff;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
  }
  return ~c;
}

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new contra();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

static void load(const MameFrame &m) {
  memcpy(machine->ctrl, m.ctrl, sizeof(m.ctrl));
  uint8_t *ram = machine->ram;
  memcpy(&ram[0x0c00], m.palette, sizeof(m.palette));
  memcpy(&ram[0x2000], m.layer0_2, sizeof(m.layer0_2));
  memcpy(&ram[0x4000], m.layer1, sizeof(m.layer1));
  // sprite lists: MAME latches them at the vblank before the drawn frame
  memcpy(&ram[0x3000], m.prev_sprites0, sizeof(m.prev_sprites0));
  memcpy(&ram[0x5000], m.prev_sprites1, sizeof(m.prev_sprites1));
  machine->publishVideo();
}

static void render(void) {
  machine->prepare_frame();
  for (int row = 0; row < PANEL_LINES / 8; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
  }
}

static void writePpm(uint32_t frame) {
  const char *dir = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof(path), "%s/contra_%u.ppm", dir ? dir : "/tmp", frame);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", WIDTH, PANEL_LINES);
  for (int y = 0; y < PANEL_LINES; y++) {
    for (int x = 0; x < WIDTH; x++) {
      const uint16_t v = (uint16_t)((screen[y][x] << 8) | (screen[y][x] >> 8));
      const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc), (uint8_t)((v << 3) & 0xf8)};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
  printf("wrote %s\n", path);
}

static void test_render_matches_mame(void) {
  boot();
  uint32_t bad_total = 0;
  for (const MameFrame &m : mame_frames) {
    load(m);
    render();
    uint32_t bad = 0;
    int first = -1;
    for (int y = 0; y < HEIGHT; y++) {
      if (crc32((const uint8_t *)screen[TOP + y], WIDTH * 2) != m.line_crc[y]) {
        bad++;
        first = first < 0 ? y : first;
      }
    }
    printf("frame %u: %u of %d lines differ (first %d)\n", m.frame, bad, HEIGHT, first);
    if (bad) {
      writePpm(m.frame);
    }
    bad_total += bad;
  }
  TEST_ASSERT_EQUAL_UINT32(0, bad_total);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_render_matches_mame);
#endif
  return UNITY_END();
}
