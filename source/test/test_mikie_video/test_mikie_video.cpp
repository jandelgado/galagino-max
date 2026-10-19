#include <stdio.h>
#include <string.h>
#include <unity.h>

// Renders MAME mikie frames from their RAM and compares every visible
// line with MAME's snapshot. ROM headers come from romconv
// (./convert.sh mikie); without them the tests are skipped.
#if __has_include("../../src/machines/mikie/mikie_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_MIKIE
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/mikie/mikie.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int WIDTH = 224, HEIGHT = 288, ROWS = HEIGHT / 8;
static const int TOP = 16, VISIBLE = 256;   // panel rows of MAME's 224x256 view
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static mikie *machine;

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
void Reset6502(M6502 *) {}
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
  machine = new mikie();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->game_started = 1;
}

enum SpriteRam { SPRITES_SAME_FRAME, SPRITES_PREV_FRAME };

// RAM 0x2800-0x3FFF as the game wrote it, then the vblank latch and the render
static void load(const MameFrame &m, SpriteRam spr) {
  memcpy(memory + MIKIE_RAM, m.ram, sizeof(m.ram));
  if (spr == SPRITES_PREV_FRAME) {
    memcpy(memory + MIKIE_RAM, m.prev, MIKIE_SPRITE_BYTES);
  }
  machine->palette_bank = m.taps[0];
  machine->publish_video();
  machine->prepare_frame();
}

static void render(void) {
  for (int row = 0; row < ROWS; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[8 * row], frame_buffer, sizeof(frame_buffer));
  }
}

static int badLines(const MameFrame &m, SpriteRam spr, int *first) {
  load(m, spr);
  render();
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
    snprintf(name, sizeof(name), "render_%u.raw", m.frame);
    FILE *f = fopen(name, "wb");
    fwrite(screen[TOP], sizeof(screen[0]) * VISIBLE, 1, f);
    fclose(f);
    char msg[96];
    snprintf(msg, sizeof(msg), "frame %u: %d lines differ, first %d (dump %s)", m.frame, bad, first, name);
    TEST_FAIL_MESSAGE(msg);
  }
}

// Boot the real ROMs from reset through both CPUs: the static title
// screen must render as MAME's snapshot of the same frame.
static void test_boot_title(void) {
  boot();
  machine->reset();
  machine->game_started = 1;
  const MameFrame &m = MAME_FRAMES[0];
  for (uint32_t f = 0; f < m.frame; f++) {
    machine->run_frame();
  }
  machine->prepare_frame();
  render();
  int bad = 0;
  for (int y = 0; y < VISIBLE; y++) {
    bad += crc32((const uint8_t *)screen[TOP + y], sizeof(screen[0])) != m.line_crc[y];
  }
  TEST_ASSERT_EQUAL_INT(0, bad);
}

static void test_title_780(void) { checkFrame(MAME_FRAMES[0]); }
static void test_attract_900(void) { checkFrame(MAME_FRAMES[1]); }
static void test_demo_1500(void) { checkFrame(MAME_FRAMES[2]); }
static void test_demo_2000(void) { checkFrame(MAME_FRAMES[3]); }
static void test_demo_2300(void) { checkFrame(MAME_FRAMES[4]); }
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boot_title);
  RUN_TEST(test_title_780);
  RUN_TEST(test_attract_900);
  RUN_TEST(test_demo_1500);
  RUN_TEST(test_demo_2000);
  RUN_TEST(test_demo_2300);
#endif
  return UNITY_END();
}
