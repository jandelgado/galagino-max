#include <stdio.h>
#include <string.h>
#include <unity.h>

// Renders MAME digdug2 frames from their video RAM and compares every
// panel line with MAME's snapshot. ROM headers come from romconv
// (./convert.sh digdug2); without them the tests are skipped. The
// machine's video latch and render state are private: this test opens
// them up.
#if __has_include("../../src/machines/digdug2/digdug2_rom_main.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/emulation/namco_wsg.cpp"
#define private public
#define protected public
#include "../../src/machines/digdug2/digdug2.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int WIDTH = 224, HEIGHT = 288, ROWS = HEIGHT / 8;
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static unsigned short screen[HEIGHT][WIDTH];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static digdug2 *machine;

unsigned int Input::buttons_get(void) { return 0; }
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
  machine = new digdug2();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

// RAM as the game wrote it, then the vblank latch and the render
static void load(const MameFrame &m) {
  memcpy(memory, m.vram, sizeof(m.vram));
  memcpy(memory + SPRITE_TAB1, m.tab1, sizeof(m.tab1));
  memcpy(memory + SPRITE_TAB2, m.tab2, sizeof(m.tab2));
  memcpy(memory + SPRITE_TAB3, m.tab3, sizeof(m.tab3));
  machine->m6809_write(&machine->main_cpu, 0x3800 + (m.scroll << 3), 0);
  machine->vblank.publish([](digdug2::VideoState &v) {
    memcpy(v.tab1, memory + SPRITE_TAB1, sizeof(v.tab1));
    memcpy(v.tab2, memory + SPRITE_TAB2, sizeof(v.tab2));
    memcpy(v.tab3, memory + SPRITE_TAB3, sizeof(v.tab3));
    v.scroll = machine->scroll;
  });
  machine->prepare_frame();
}

static void render(void) {
  for (int row = 0; row < ROWS; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[8 * row], frame_buffer, sizeof(frame_buffer));
  }
}

static void checkFrame(const MameFrame &m) {
  boot();
  load(m);
  render();
  int bad = 0, first = -1;
  for (int y = 0; y < HEIGHT; y++) {
    if (crc32((const uint8_t *)screen[y], sizeof(screen[y])) != m.line_crc[y]) {
      bad++;
      first = first < 0 ? y : first;
    }
  }
  if (bad) {
    char name[64];
    snprintf(name, sizeof(name), "render_%u.raw", m.frame);
    FILE *f = fopen(name, "wb");
    fwrite(screen, sizeof(screen), 1, f);
    fclose(f);
    char msg[96];
    snprintf(msg, sizeof(msg), "frame %u: %d lines differ, first %d (dump %s)", m.frame, bad, first, name);
    TEST_FAIL_MESSAGE(msg);
  }
}

static void test_attract_600(void) { checkFrame(MAME_FRAMES[0]); }
static void test_attract_1200(void) { checkFrame(MAME_FRAMES[1]); }
static void test_attract_1800(void) { checkFrame(MAME_FRAMES[2]); }
static void test_game_over_2400(void) { checkFrame(MAME_FRAMES[3]); }
static void test_game_3200(void) { checkFrame(MAME_FRAMES[4]); }
static void test_game_4000(void) { checkFrame(MAME_FRAMES[5]); }

// sprite codes 128-255 live in the second half of the 2x 16 KB sprite ROM
// (todruaga has 128): code 0x80+ must render its own pixels, not code & 0x7F
static void test_high_sprite_code(void) {
  boot();
  int code = -1;
  for (int c = 0x80; c < 0x100 && code < 0; c++) {
    if (memcmp(machine->digdug2_sprites[c], machine->digdug2_sprites[c & 0x7F],
               sizeof(machine->digdug2_sprites[c])) != 0) {
      code = c;
    }
  }
  TEST_ASSERT_TRUE(code > 0);

  MameFrame m = {};
  memset(m.tab3, 0, sizeof(m.tab3));
  for (int offs = 0; offs < 0x80; offs += 2) {
    m.tab3[offs + 1] = 2;   // all disabled
  }
  m.tab1[0] = code;
  m.tab1[1] = 1;            // color group 1
  m.tab2[0] = 100;          // Y
  m.tab2[1] = 140;          // X
  m.tab3[1] = 0;            // enabled
  load(m);

  TEST_ASSERT_EQUAL_INT(1, machine->active_sprites);
  TEST_ASSERT_EQUAL_INT(code, sprites[0].code);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_high_sprite_code);
  RUN_TEST(test_attract_600);
  RUN_TEST(test_attract_1200);
  RUN_TEST(test_attract_1800);
  RUN_TEST(test_game_over_2400);
  RUN_TEST(test_game_3200);
  RUN_TEST(test_game_4000);
#endif
  return UNITY_END();
}
