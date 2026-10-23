#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "../../src/machines/solomon/solomon_video.h"

// Solomon's Key: pure geometry, plus (with the romconv headers from
// ./convert.sh solomon) the real ROM booted through the machine class.
// The machine's video latch and render state are private: this test
// opens them up. Arduino.h / FastLED.h / esp_attr.h here are host stubs.
#if __has_include("../../src/machines/solomon/solomon_main_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/solomon/solomon.cpp"
#undef private
#undef protected
#include "mame_frames.h"
#endif

using namespace solomon_video;

void setUp(void) {}
void tearDown(void) {}

// HUD lines keep both screen edges and drop 8 / 16 empty columns
static void test_hud_columns(void) {
  TEST_ASSERT_EQUAL_UINT8(15, hudTopX(15));
  TEST_ASSERT_EQUAL_UINT8(24, hudTopX(16));
  TEST_ASSERT_EQUAL_UINT8(247, hudTopX(OUT_WIDTH - 1));
  TEST_ASSERT_EQUAL_UINT8(0, hudBottomX(0));
  TEST_ASSERT_EQUAL_UINT8(119, hudBottomX(119));
  TEST_ASSERT_EQUAL_UINT8(136, hudBottomX(120));
  TEST_ASSERT_EQUAL_UINT8(255, hudBottomX(OUT_WIDTH - 1));
}

// left wall -> 0, right wall -> 16, monotonic between
static void test_scroll_target(void) {
  TEST_ASSERT_EQUAL_UINT8(0, scrollFor(0));
  TEST_ASSERT_EQUAL_UINT8(0, scrollFor(PLAYER_X_MIN));
  TEST_ASSERT_EQUAL_UINT8(CROP_LEFT, scrollFor((PLAYER_X_MIN + PLAYER_X_MAX) / 2));
  TEST_ASSERT_EQUAL_UINT8(SCROLL_MAX, scrollFor(PLAYER_X_MAX));
  TEST_ASSERT_EQUAL_UINT8(SCROLL_MAX, scrollFor(255));
  for (int x = 1; x < 256; x++) {
    TEST_ASSERT_TRUE(scrollFor(x) >= scrollFor(x - 1));
  }
}

// every native x except x % 16 == 15, ascending
static void test_scale_columns(void) {
  int prev = -1;
  for (int c = 0; c < OUT_WIDTH; c++) {
    const int x = scaleX(c);
    TEST_ASSERT_TRUE(x > prev);
    TEST_ASSERT_NOT_EQUAL(15, x % 16);
    TEST_ASSERT_TRUE(x == prev + 1 || x == prev + 2);
    prev = x;
  }
  TEST_ASSERT_EQUAL_INT(254, prev);
}

static void test_sprite_fields(void) {
  TEST_ASSERT_EQUAL_INT(241 - 0x50 - VIS_TOP, spriteLine(0x50));
  TEST_ASSERT_EQUAL_UINT16(0x1ab, spriteCode(0xab, 0x10));
  TEST_ASSERT_EQUAL_UINT8(7, spriteColor(0x0e));
  TEST_ASSERT_EQUAL_UINT16(0x7ff, tileCode(0xff, 0x07));
  TEST_ASSERT_EQUAL_UINT8(7, tileColor(0x70));
}

// xBGR_444 lo = GGGGRRRR, hi = xxxxBBBB -> RGB565, bytes swapped
static void test_palette(void) {
  TEST_ASSERT_EQUAL_HEX16(0x00f8, paletteColor(0x0f, 0x00)); // red
  TEST_ASSERT_EQUAL_HEX16(0xe007, paletteColor(0xf0, 0x00)); // green
  TEST_ASSERT_EQUAL_HEX16(0x1f00, paletteColor(0x00, 0x0f)); // blue
  TEST_ASSERT_EQUAL_HEX16(0xffff, paletteColor(0xff, 0x0f));
}

#ifdef HAVE_ROM
static solomon *machine;
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opPrefixZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static Input input;
static unsigned short frame_buffer[OUT_WIDTH * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new solomon();
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static int frames;      // since boot, all tests share one machine
static int audio_peak;  // max |sample| since the last reset by a test
static const int AUDIO_CHUNKS = 6;  // Audio::transmit() per screen

// runs up to absolute frame `until` holding keys k, audio as main.cpp
static void runUntil(int until, unsigned int k) {
  for (; frames < until; frames++) {
    keys = k;
    machine->run_frame();
    for (int c = 0; c < AUDIO_CHUNKS; c++) {
      int16_t out[64];
      machine->renderAudio(out, 64);
      for (int i = 0; i < 64; i++) {
        audio_peak = abs(out[i]) > audio_peak ? abs(out[i]) : audio_peak;
      }
    }
  }
  keys = 0;
}

// title loop: the game enables the vblank NMI
static void test_boot_attract(void) {
  boot();
  runUntil(600, 0);
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_TRUE(machine->nmi_mask);
}

// e603 hack keyed on PC (MAME _0xe603_r)
static void test_e603_hack(void) {
  boot();
  const unsigned short pc = machine->cpu[0].PC.W;
  const unsigned short bc = machine->cpu[0].BC.W;
  machine->current_cpu = 0;
  machine->cpu[0].PC.W = 0x161;
  TEST_ASSERT_EQUAL_UINT8(0, machine->rdZ80(0xe603));
  machine->cpu[0].PC.W = 0x4cf0;
  machine->cpu[0].BC.W = 0x00ff;
  TEST_ASSERT_EQUAL_UINT8(0x08, machine->rdZ80(0xe603));
  machine->cpu[0].BC.W = 0x00f7;
  TEST_ASSERT_EQUAL_UINT8(0x00, machine->rdZ80(0xe603));
  machine->cpu[0].PC.W = 0x1234;
  TEST_ASSERT_EQUAL_UINT8(0, machine->rdZ80(0xe603));
  machine->cpu[0].PC.W = pc;
  machine->cpu[0].BC.W = bc;
}

// one sprite at native x sx, screen y 100 (output lines 84..99) on an
// empty screen (all tiles code 0, palette entry i = i + 1, so sprite pens
// differ from the bg color); counts its pixels in output columns
// [0, 16) and [OUT_WIDTH - 16, OUT_WIDTH)
static const int SPR_LINE = 84;
static void spritePixels(int sx, int *left, int *right) {
  boot();
  uint16_t saved_palette[256];
  memcpy(saved_palette, machine->palette, sizeof(saved_palette));
  memset(&machine->video, 0, sizeof(machine->video));
  for (int i = 0; i < 256; i++) {
    machine->palette[i] = (uint16_t)(i + 1);
  }
  int code = -1;  // first sprite with pixels in both 8 px halves
  for (int c = 0; c < 512 && code < 0; c++) {
    uint32_t lo = 0, hi = 0;
    for (int r = 0; r < 16; r++) {
      lo |= machine->sprites[c][r][0];
      hi |= machine->sprites[c][r][1];
    }
    code = lo && hi ? c : -1;
  }
  TEST_ASSERT_TRUE(code >= 0);
  for (int o = 0; o < SPRITE_BYTES; o += 4) {
    machine->video.spr[o + 2] = 0;  // screen y 241: below the visible area
  }
  uint8_t *s = machine->video.spr;
  s[0] = code & 0xff;
  s[1] = (code >> 4) & 0x10;
  s[2] = (uint8_t)(SPRITE_Y_BASE - (SPR_LINE + VIS_TOP));
  s[3] = (uint8_t)sx;
  machine->prepare_frame_sprites();

  const uint16_t bg = machine->palette[BG_PALETTE];
  *left = *right = 0;
  for (int line = SPR_LINE; line < SPR_LINE + SPRITE_SIZE; line++) {
    uint16_t dst[OUT_WIDTH];
    machine->render_line(dst, line);
    for (int c = 0; c < OUT_WIDTH; c++) {
      if (dst[c] == bg) {
        continue;
      }
      *left += c < 16;
      *right += c >= OUT_WIDTH - 16;
    }
  }
  memcpy(machine->palette, saved_palette, sizeof(saved_palette));  // shared machine
}

// sx 236: native x 236..251, crop shows 236..247 at the right edge
static void test_sprite_clip_right(void) {
  int left, right;
  spritePixels(236, &left, &right);
  TEST_ASSERT_GREATER_THAN_INT(0, right);
  TEST_ASSERT_EQUAL_INT(0, left);
}

// sx 250: native x 250..255 only, all cropped; never wraps to the left
static void test_sprite_no_wrap(void) {
  int left, right;
  spritePixels(250, &left, &right);
  TEST_ASSERT_EQUAL_INT(0, left);
  TEST_ASSERT_EQUAL_INT(0, right);
}
// Dana (slot 28) at the right wall: 1 px per frame to 16; gone: back to 8
static void test_follow_player(void) {
  boot();
  const solomon::VideoState saved = machine->video;
  memset(&machine->video, 0, sizeof(machine->video));
  uint8_t *s = machine->video.spr + PLAYER_SPRITE;
  s[1] = PLAYER_COLOR << 1;
  s[2] = (uint8_t)(SPRITE_Y_BASE - (100 + VIS_TOP));
  s[3] = PLAYER_X_MAX;
  machine->scroll_x = CROP_LEFT;

  machine->followPlayer();
  TEST_ASSERT_EQUAL_UINT8(CROP_LEFT + 1, machine->scroll_x);
  for (int i = 0; i < SCROLL_MAX; i++) {
    machine->followPlayer();
  }
  TEST_ASSERT_EQUAL_UINT8(SCROLL_MAX, machine->scroll_x);

  s[2] = 0;  // screen y 241: off screen
  for (int i = 0; i < SCROLL_MAX; i++) {
    machine->followPlayer();
  }
  TEST_ASSERT_EQUAL_UINT8(CROP_LEFT, machine->scroll_x);

  // other color in slot 28: not Dana
  s[1] = 0;
  s[2] = (uint8_t)(SPRITE_Y_BASE - (100 + VIS_TOP));
  machine->followPlayer();
  TEST_ASSERT_EQUAL_UINT8(CROP_LEFT, machine->scroll_x);
  machine->video = saved;
}

// RGB565, bytes swapped for SPI -> 8 bit RGB
static void putPixel(FILE *f, uint16_t v) {
  v = (uint16_t)((v << 8) | (v >> 8));
  const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc),
                          (uint8_t)((v << 3) & 0xf8)};
  fwrite(rgb, 1, 3, f);
}

// one screen through prepare_frame/render_row into $TMPDIR/<name>
static void writePpm(const char *name) {
  machine->prepare_frame();
  const char *dir = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", dir ? dir : "/tmp", name);
  FILE *f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL(f);
  fprintf(f, "P6\n%d 288\n255\n", OUT_WIDTH);
  for (int row = 0; row < 36; row++) {
    machine->render_row(row);
    for (int i = 0; i < OUT_WIDTH * 8; i++) {
      putPixel(f, frame_buffer[i]);
    }
  }
  fclose(f);
  printf("written %s\n", path);
}


// coin, start at the frames mame_fixture.lua uses, so the PPM can be
// compared with MAME's frame 2400: the sound board plays (AY output) and
// round 1 is drawn
static void test_coin_start_plays(void) {
  boot();
  runUntil(2000, 0);
  runUntil(2006, BUTTON_COIN);
  runUntil(2100, 0);
  audio_peak = 0;
  runUntil(2106, BUTTON_START);
  runUntil(2400, 0);
  writePpm("solomon_game.ppm");

  // same inputs as MAME: palette RAM equal at frame 2400 (static in the round)
  int pal_diff = 0;
  for (int i = 0; i < 0x200; i++) {
    const uint8_t v = memory[SOL_PALETTE_RAM + i];
    if (v != MAME_FRAMES[2].pal[i]) {
      if (pal_diff++ < 8) {
        printf("palette %03x: %02x, MAME %02x\n", i, v, MAME_FRAMES[2].pal[i]);
      }
    }
  }
  // video RAM too: same round layout as MAME
  const int vram_diff = memcmp(memory + SOL_FG_COLOR, MAME_FRAMES[2].vram, 0x1000);
  TEST_ASSERT_EQUAL_INT(0, vram_diff);
  TEST_ASSERT_EQUAL_INT(2400, MAME_FRAMES[2].frame);
  TEST_ASSERT_EQUAL_INT(0, pal_diff);

  printf("audio peak %d since start\n", audio_peak);
  TEST_ASSERT_GREATER_THAN_INT(0, audio_peak);
  TEST_ASSERT_LESS_OR_EQUAL_INT(AUDIO_MAX, audio_peak);
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

// MAME RAM written through the bus, then latch + render; every 240 px
// line must equal MAME's snapshot cropped as crop mode, HUD lines
// included (scale shares the gather and is covered by test_scale_columns)
static void checkFrame(const MameFrame &m) {
  boot();
  machine->current_cpu = 0;
  for (int i = 0; i < 0x1000; i++) {
    machine->wrZ80(0xd000 + i, m.vram[i]);
  }
  for (int i = 0; i < 0x200; i++) {
    machine->wrZ80(0xe400 + i, m.pal[i]);
  }
  for (int i = 0; i < SPRITE_BYTES; i++) {
    machine->wrZ80(0xe000 + i, m.spr[i]);
  }
  machine->publishVideo();
  machine->prepare_frame();

  int bad = 0, first = -1;
  static uint16_t screen[VIS_LINES][OUT_WIDTH];
  for (int line = 0; line < VIS_LINES; line++) {
    machine->render_line(screen[line], line);
    if (crc32((const uint8_t *)screen[line], sizeof(screen[line])) != m.line_crc[line]) {
      bad++;
      first = first < 0 ? line : first;
    }
  }
  if (bad) {
    char name[64];
    snprintf(name, sizeof(name), "/tmp/solomon_render_%u.raw", m.frame);
    FILE *f = fopen(name, "wb");
    fwrite(screen, sizeof(screen), 1, f);
    fclose(f);
  }
  char msg[96];
  snprintf(msg, sizeof(msg), "frame %u: %d lines differ, first %d", m.frame, bad, first);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad, msg);
}

static void test_mame_title(void) { checkFrame(MAME_FRAMES[0]); }
static void test_mame_demo(void) { checkFrame(MAME_FRAMES[1]); }
static void test_mame_game(void) { checkFrame(MAME_FRAMES[2]); }
static void test_mame_game_later(void) { checkFrame(MAME_FRAMES[3]); }
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_hud_columns);
  RUN_TEST(test_scroll_target);
  RUN_TEST(test_scale_columns);
  RUN_TEST(test_sprite_fields);
  RUN_TEST(test_palette);
#ifdef HAVE_ROM
  RUN_TEST(test_boot_attract);
  RUN_TEST(test_e603_hack);
  RUN_TEST(test_sprite_clip_right);
  RUN_TEST(test_sprite_no_wrap);
  RUN_TEST(test_follow_player);
  RUN_TEST(test_coin_start_plays);
  RUN_TEST(test_mame_title);
  RUN_TEST(test_mame_demo);
  RUN_TEST(test_mame_game);
  RUN_TEST(test_mame_game_later);
#endif
  return UNITY_END();
}
