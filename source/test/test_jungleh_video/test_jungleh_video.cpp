#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Jungle Hunt video (ROT180): render against MAME reference frames. ROM
// headers come from romconv (./convert.sh jungleh); without them
// the test is skipped. Arduino.h / FastLED.h / esp_attr.h are host stubs.
#if __has_include("../../src/machines/jungleh/jungleh_main_rom.h")
#define HAVE_ROM 1
#define ENABLE_JUNGLEH
#define ELEVATOR_Z80_TABLES_INCLUDED
#include "mame_frames.h"
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/elevator/elevator.cpp"
#include "../../src/machines/jungleh/jungleh.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static jungleh *machine = nullptr;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}


static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint16_t screen[288][240];

static const int PANEL_LINE0 = 8 * 4;   // strip 4 = native line 16

static void create(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new jungleh();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

// MAME state blob (mame_fixture.lua order) -> published VideoState
static void publish(const uint8_t *st) {
  machine->vblank.publish([st](elevator::VideoState &v) {
    const uint8_t *p = st;
    memcpy(v.tilemap, p, sizeof(v.tilemap)); p += sizeof(v.tilemap);
    memcpy(v.colscroll, p, sizeof(v.colscroll)); p += sizeof(v.colscroll);
    memcpy(v.spr, p, sizeof(v.spr)); p += sizeof(v.spr);
    memcpy(v.pal, p, sizeof(v.pal)); p += sizeof(v.pal);
    memcpy(v.scroll, p, sizeof(v.scroll)); p += sizeof(v.scroll);
    memcpy(v.colorbank, p, sizeof(v.colorbank)); p += sizeof(v.colorbank);
    v.mode = *p++;
    v.prio = *p;
  });
}

static void renderScreen(void) {
  machine->prepare_frame();
  for (int row = 0; row < 36; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    memcpy(screen[row * 8], frame_buffer, sizeof(frame_buffer));
  }
}

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

static void writePpm(const char *name) {
  const char *dir = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir ? dir : "/tmp", name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    return;
  }
  fprintf(f, "P6\n240 288\n255\n");
  for (int y = 0; y < 288; y++) {
    for (int x = 0; x < 240; x++) {
      const uint16_t v = (uint16_t)((screen[y][x] << 8) | (screen[y][x] >> 8));
      const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc), (uint8_t)((v << 3) & 0xf8)};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
  printf("wrote %s\n", path);
}

// every line of 5 attract frames (vines, river with sprites; the game sets
// flip x+y) equals the MAME snapshot (already turned 180 degrees), columns
// 8..247. Fixture: state of frame F (mame_fixture.py STATE=cur)
static void test_render_matches_mame(void) {
  create();
  for (const MameFrame &m : MAME_FRAMES) {
    memcpy(machine->charram, m.charram, sizeof(m.charram));
    publish(m.state);
    renderScreen();

    int bad = 0;
    for (int y = 0; y < 224; y++) {
      const uint32_t c = crc32((const uint8_t *)screen[PANEL_LINE0 + y], 240 * 2);
      bad += c != m.line_crc[y];
    }
    printf("frame %u: %d of 224 lines differ\n", m.frame, bad);
    if (bad) {
      char name[32];
      snprintf(name, sizeof(name), "jungleh_%u", m.frame);
      writePpm(name);
    }
    TEST_ASSERT_EQUAL_INT(0, bad);
  }
}

// ROT180 with flip bits clear (strip turned by the renderer): sprite at
// native x 250 (RAM 251) wraps to native x 8..9, which
// lands on panel columns 239..238 (c = 247 - x); native line y lands on
// panel line 239 - y
static void test_sprite_wrap_rot180(void) {
  create();
  memset(machine->charram, 0, elevator::CHARRAM_SIZE);
  memset(machine->charram, 0xff, 32);   // sprite 0, plane 0: pen 1

  uint8_t st[0xd6a] = {};
  uint8_t *spr = st + 0xc00 + 0x60;
  uint8_t *pal = spr + 0x80;
  uint8_t *regs = pal + 0x80;   // scroll[6], colorbank[2], mode, prio
  spr[0] = 251;                 // sx = 250
  spr[1] = 240 - 100;           // sy = 100
  pal[2 * 1 + 1] = 0x3f;        // pen 1 red, pen 0 white (all 0)
  pal[2 * 1] = 0x00;
  for (int i = 0; i < 64; i++) {
    if (i != 1) {
      pal[2 * i] = 0x01;        // black
      pal[2 * i + 1] = 0xff;
    }
  }
  regs[8] = 0x80;               // sprites on, tilemaps off
  regs[9] = 0x06;
  publish(st);
  renderScreen();

  const int line = PANEL_LINE0 + 239 - (100 + 4);   // native line 104
  TEST_ASSERT_EQUAL_HEX16(0x00f8, screen[line][239]);
  TEST_ASSERT_EQUAL_HEX16(0x00f8, screen[line][238]);
  TEST_ASSERT_EQUAL_HEX16(0x0000, screen[line][237]);
  TEST_ASSERT_EQUAL_HEX16(0x0000, screen[line][0]);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_render_matches_mame);
  RUN_TEST(test_sprite_wrap_rot180);
#endif
  return UNITY_END();
}
