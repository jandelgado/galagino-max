#include <stdio.h>
#include <string.h>
#include <unity.h>

// Renders MAME arkangc frames from their video RAM and compares every
// visible line with MAME's snapshot. ROM headers come from romconv
// (./convert.sh arkanoid); without them the tests are skipped.
#if __has_include("../../src/machines/arkanoid/arkanoid_rom.h")
#define HAVE_ROM 1
#define ENABLE_ARKANOID
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/arkanoid/arkanoid.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int WIDTH = 224, HEIGHT = 288, ROWS = HEIGHT / 8;
static const int TOP = 16, VISIBLE = 256;   // panel lines of MAME's 224x256 view
static const int VIDEO_BYTES = 0x840;       // e000-e83f, then the d008 latch
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static arkanoid *machine;

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
  machine = new arkanoid();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->game_started = 1;
}

enum SpriteRam { SPRITES_SAME_FRAME, SPRITES_PREV_FRAME };

// video RAM and d008 as the game wrote them, then the vblank latch
static void load(const MameFrame &m, SpriteRam spr) {
  const uint8_t *src = spr == SPRITES_PREV_FRAME ? m.prev : m.ram;
  memcpy(memory + ARKANOID_VRAM, m.ram, 0x800);
  memcpy(memory + ARKANOID_SPR, src + 0x800, 0x40);
  machine->ctrl = m.ram[VIDEO_BYTES];
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

// a sprite partly off the right/bottom edge draws its visible part only,
// nothing wraps to the opposite edge
static void test_sprite_clip(void) {
  boot();
  memset(memory + ARKANOID_VRAM, 0, 0x840);
  machine->ctrl = 0;
  machine->publish_video();
  machine->prepare_frame();
  render();
  static unsigned short blank[HEIGHT][WIDTH];
  memcpy(blank, screen, sizeof(screen));

  // find a code whose two chars have pixels in every native column
  int code = -1;
  for (int c = 1; c < 1024 && code < 0; c++) {
    bool full = true;
    for (int r = 0; r < 8; r++) {
      full = full && machine->tiles[machine->tile_map[2 * c]][r] && machine->tiles[machine->tile_map[2 * c + 1]][r];
    }
    code = full ? c : -1;
  }
  TEST_ASSERT_TRUE(code > 0);

  uint8_t *s = memory + ARKANOID_SPR;
  s[0] = 252;                 // native X 252..259: lines 268..271 only
  s[1] = SPRITE_Y_BASE - 236; // native Y 228..243: columns 0..11 at most
  s[2] = (code >> 8) & 0x03;
  s[3] = code & 0xFF;
  machine->publish_video();
  machine->prepare_frame();
  render();
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      if (screen[y][x] == blank[y][x]) {
        continue;
      }
      TEST_ASSERT_TRUE_MESSAGE(y >= TOP + 252 && y < TOP + 256, "sprite line outside native X 252..255");
      TEST_ASSERT_TRUE_MESSAGE(x <= VIS_BOTTOM - 228, "sprite column outside native Y 228..239");
    }
  }
}

static void test_title_400(void) { checkFrame(MAME_FRAMES[0]); }
static void test_bank1_600(void) { checkFrame(MAME_FRAMES[1]); }
static void test_demo_1300(void) { checkFrame(MAME_FRAMES[2]); }
static void test_demo_2100(void) { checkFrame(MAME_FRAMES[3]); }
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_title_400);
  RUN_TEST(test_bank1_600);
  RUN_TEST(test_demo_1300);
  RUN_TEST(test_demo_2100);
  RUN_TEST(test_sprite_clip);
#endif
  return UNITY_END();
}
