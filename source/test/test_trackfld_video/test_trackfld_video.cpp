#include <stdio.h>
#include <string.h>
#include <unity.h>

// Renders MAME trackfld frames from their RAM and compares every visible line
// with MAME's snapshot; boots the real ROM from reset to attract mode. ROM
// headers come from romconv (./convert.sh trackfld); without them the tests
// are skipped.
#if __has_include("../../src/machines/trackfld/trackfld_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_TRACKFLD
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#include "../../src/emulation/vlm5030.cpp"
#define private public
#define protected public
#include "../../src/machines/trackfld/trackfld.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int WIDTH = 240, HEIGHT = 288, ROWS = HEIGHT / 8;
static const int TOP = 32, VISIBLE = 224;  // panel rows of MAME's 256x224 view
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static trackfld *machine;

unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return 0xff; }
void OutZ80(unsigned short p, unsigned char v) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }

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
  machine = new trackfld();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

static void render(void) {
  for (int row = 0; row < ROWS; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[8 * row], frame_buffer, sizeof(frame_buffer));
  }
}

static void dump(const char *name) {
  FILE *f = fopen(name, "wb");
  fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      const uint16_t v = (uint16_t)((screen[y][x] << 8) | (screen[y][x] >> 8));
      const uint8_t rgb[3] = {(uint8_t)((v >> 11) << 3), (uint8_t)(((v >> 5) & 63) << 2),
                              (uint8_t)((v & 31) << 3)};
      fwrite(rgb, 3, 1, f);
    }
  }
  fclose(f);
}

static int countBad(const MameFrame &m, int *first) {
  int bad = 0;
  *first = -1;
  for (int y = 0; y < VISIBLE; y++) {
    if (crc32((const uint8_t *)screen[TOP + y], sizeof(screen[0])) != m.line_crc[y]) {
      bad++;
      *first = *first < 0 ? y : *first;
    }
  }
  return bad;
}

enum SpriteRam { SPRITES_SAME_FRAME, SPRITES_PREV_FRAME };

static int badLines(const MameFrame &m, SpriteRam spr, int *first) {
  // fixture: RAM 0x1800-0x1fff (2 KB), then 0x3000-0x3fff (4 KB)
  memcpy(memory + TF_MEM_RAM, m.ram, 0x800);
  memcpy(memory + TF_MEM_VRAM, m.ram + 0x800, 0x1000);
  if (spr == SPRITES_PREV_FRAME) {
    memcpy(memory + TF_MEM_SPR2, m.prev + TF_MEM_SPR2, 0x40);
    memcpy(memory + TF_MEM_SPR, m.prev + TF_MEM_SPR, 0x40);
  }
  machine->publish_video();
  machine->prepare_frame();
  render();
  return countBad(m, first);
}

// MAME may draw sprites from this or the previous frame's RAM: accept either
static void checkFrame(const MameFrame &m) {
  boot();
  int first = -1;
  if (badLines(m, SPRITES_SAME_FRAME, &first) == 0) {
    return;
  }
  const int bad = badLines(m, SPRITES_PREV_FRAME, &first);
  if (bad) {
    char name[64];
    snprintf(name, sizeof(name), ".pio/trackfld_render_%u.ppm", m.frame);
    dump(name);
    char msg[128];
    snprintf(msg, sizeof(msg), "frame %u: %d lines differ, first %d (dump %s)", m.frame, bad, first, name);
    TEST_FAIL_MESSAGE(msg);
  }
}

static uint32_t frameHash(void) {
  return crc32((const uint8_t *)screen, sizeof(screen));
}

// From reset on the real ROM: the attract mode must animate and reach the
// fight demo (tiles cover the playfield, sprites move)
static void test_boot_attract(void) {
  boot();
  machine->reset();
  uint32_t hashes[4];
  for (int i = 0; i < 4; i++) {
    for (int f = 0; f < 200; f++) {
      machine->run_frame();
    }
    machine->prepare_frame();
    render();
    hashes[i] = frameHash();
    char name[64];
    snprintf(name, sizeof(name), ".pio/trackfld_boot_%d.ppm", (i + 1) * 200);
    dump(name);
  }
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_NOT_EQUAL(hashes[0], hashes[1]);
  TEST_ASSERT_NOT_EQUAL(hashes[2], hashes[3]);
}

// The static title from reset on the real ROM: same lines as MAME's frame
static void test_boot_title(void) {
  boot();
  machine->reset();
  const MameFrame &m = MAME_FRAMES[0];
  for (uint32_t f = 0; f < m.frame; f++) {
    machine->run_frame();
  }
  machine->prepare_frame();
  render();
  int first = -1;
  TEST_ASSERT_EQUAL_INT(0, countBad(m, &first));
}

static void test_title_300(void) { checkFrame(MAME_FRAMES[0]); }
static void test_long_jump_800(void) { checkFrame(MAME_FRAMES[1]); }
static void test_long_jump_1300(void) { checkFrame(MAME_FRAMES[2]); }
static void test_ranking_1900(void) { checkFrame(MAME_FRAMES[3]); }
static void test_hurdles_2900(void) { checkFrame(MAME_FRAMES[4]); }
static void test_hurdles_3000(void) { checkFrame(MAME_FRAMES[5]); }
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boot_attract);
  RUN_TEST(test_boot_title);
  RUN_TEST(test_title_300);
  RUN_TEST(test_long_jump_800);
  RUN_TEST(test_long_jump_1300);
  RUN_TEST(test_ranking_1900);
  RUN_TEST(test_hurdles_2900);
  RUN_TEST(test_hurdles_3000);
#endif
  return UNITY_END();
}
