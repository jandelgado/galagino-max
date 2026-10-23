#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Renders MAME megazone frames from their RAM and scroll registers and
// compares every panel line with MAME's snapshot. ROM headers come from
// romconv (./convert.sh megazone); without them the test is skipped.
#if __has_include("../../src/machines/megazone/megazone_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_MEGAZONE
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/cpus/i8048/i8048.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/megazone/megazone.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static Input input;
static unsigned short frame_buffer[224 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static megazone *machine;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
void i8048_port_write(i8048_state_S *s, unsigned char p, unsigned char v) { machine->wrI8048_port(s, p, v); }
unsigned char i8048_port_read(i8048_state_S *s, unsigned char p) { return machine->rdI8048_port(s, p); }
unsigned char i8048_rom_read(i8048_state_S *s, unsigned short a) { return machine->rdI8048_rom(s, a); }
unsigned char i8048_xdm_read(i8048_state_S *s, unsigned char a) { return machine->rdI8048_xdm(s, a); }
void i8048_xdm_write(i8048_state_S *, unsigned char, unsigned char) {}
}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }

static unsigned short screen[288][224];

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new megazone();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
}

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t crc = ~0u;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
  }
  return ~crc;
}

static void writePpm(const char *name) {
  const char *dir = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir ? dir : "/tmp", name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    return;
  }
  fprintf(f, "P6\n224 288\n255\n");
  for (int y = 0; y < 288; y++) {
    for (int x = 0; x < 224; x++) {
      const uint16_t v = (uint16_t)((screen[y][x] << 8) | (screen[y][x] >> 8));
      const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc),
                              (uint8_t)((v << 3) & 0xf8)};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

// title, high scores and three scrolled playfield frames with sprites:
// every line equals the MAME snapshot
static void test_render_matches_mame(void) {
  boot();
  for (const MameFrame &m : MAME_FRAMES) {
    memcpy(memory, m.ram, sizeof(m.ram));
    machine->scroll_x = m.scroll_x;
    machine->scroll_y = m.scroll_y;
    machine->publishVideo();
    machine->prepare_frame();
    for (int row = 0; row < 36; row++) {
      memset(frame_buffer, 0, sizeof(frame_buffer));
      machine->render_row(row);
      memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
    }

    int bad = 0, first = -1;
    for (int y = 0; y < 288; y++) {
      if (crc32((const uint8_t *)screen[y], sizeof(screen[y])) != m.line_crc[y]) {
        bad++;
        first = first < 0 ? y : first;
      }
    }
    printf("frame %u: %d of 288 lines differ, first %d\n", m.frame, bad, first);
    if (bad) {
      char name[32];
      snprintf(name, sizeof(name), "megazone_%u", m.frame);
      writePpm(name);
    }
    TEST_ASSERT_EQUAL_INT(0, bad);
  }
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_render_matches_mame);
#endif
  return UNITY_END();
}
