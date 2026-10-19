#include <stdio.h>
#include <string.h>
#include <unity.h>

// Renders MAME yiear frames from their RAM and compares every visible line
// with MAME's snapshot; boots the real ROM from reset to attract mode. ROM
// headers come from romconv (./convert.sh yiear); without them the tests
// are skipped.
#if __has_include("../../src/machines/yiear/yiear_rom.h")
#define HAVE_ROM 1
#define ENABLE_YIEAR
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/emulation/sn76489.cpp"
#include "../../src/emulation/vlm5030.cpp"
#define private public
#define protected public
#include "../../src/machines/yiear/yiear.cpp"
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
static yiear *machine;

unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}
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
  machine = new yiear();
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
  memcpy(memory, m.ram, sizeof(m.ram));
  if (spr == SPRITES_PREV_FRAME) {
    memcpy(memory + YIEAR_MEM_SPR0, m.prev + YIEAR_MEM_SPR0, 0x30);
    memcpy(memory + YIEAR_MEM_SPR1, m.prev + YIEAR_MEM_SPR1, 0x30);
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
    snprintf(name, sizeof(name), ".pio/yiear_render_%u.ppm", m.frame);
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
    snprintf(name, sizeof(name), ".pio/yiear_boot_%d.ppm", (i + 1) * 200);
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

static void test_title_200(void) { checkFrame(MAME_FRAMES[0]); }
static void test_ranking_450(void) { checkFrame(MAME_FRAMES[1]); }
static void test_demo_700(void) { checkFrame(MAME_FRAMES[2]); }
static void test_demo_900(void) { checkFrame(MAME_FRAMES[3]); }
static void test_demo_1200(void) { checkFrame(MAME_FRAMES[4]); }
static void test_demo_1500(void) { checkFrame(MAME_FRAMES[5]); }
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boot_attract);
  RUN_TEST(test_boot_title);
  RUN_TEST(test_title_200);
  RUN_TEST(test_ranking_450);
  RUN_TEST(test_demo_700);
  RUN_TEST(test_demo_900);
  RUN_TEST(test_demo_1200);
  RUN_TEST(test_demo_1500);
#endif
  return UNITY_END();
}
