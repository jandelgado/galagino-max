#include <stdio.h>
#include <string.h>
#include <unity.h>

// Mr. Do's screen render (scrolled background, foreground tiles) for fixed
// tile RAM and scroll registers, compared by CRC with the output of the
// original per-pixel background code. Guards render speed-ups. ROM headers
// come from romconv (./convert.sh mrdo); without them the tests are skipped.
#if __has_include("../../src/machines/mrdo/mrdo_rom1.h")
#define HAVE_ROM 1
#define ENABLE_MRDO
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#include "../../src/machines/tileaddr.cpp"
#define private public
#define protected public
#include "../../src/machines/mrdo/mrdo.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const int WIDTH = 224;
static Input input;
static unsigned short frame_buffer[WIDTH * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static mrdo *machine;

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

static uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n) {
  crc = ~crc;
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
  machine = new mrdo();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
}

// tile/colour RAM 0x0000-0x0fff from a fixed pseudo random sequence
static void fillTileRam(uint32_t seed) {
  for (int i = 0; i < 0x1000; i++) {
    seed = seed * 1103515245u + 12345u;
    memory[i] = (unsigned char)(seed >> 16);
  }
  memset(memory + 0x1000, 0, 0x100);  // no sprites
}

// CRC of all 32 visible strips as render_row draws them
static uint32_t renderCrc(unsigned char scrollx, unsigned char scrolly) {
  machine->scrollx_w = scrollx;
  machine->scrolly_w = scrolly;
  machine->prepare_frame();
  uint32_t crc = 0;
  for (short row = 0; row < 36; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    crc = crc32(crc, (const uint8_t *)frame_buffer, sizeof(frame_buffer));
  }
  return crc;
}

struct Case {
  uint32_t seed;
  unsigned char scrollx, scrolly;
  uint32_t crc;
};

// CRCs of the original per-pixel background code
static const Case CASES[] = {
  {1, 0x00, 0x00, 0x7208fe61},
  {2, 0x37, 0x00, 0xbe73dd40},
  {3, 0x00, 0xc5, 0x128cdd94},
  {4, 0xff, 0x81, 0x2b2627cc},
  {5, 0x08, 0xf8, 0xf39943a8},
};

static void test_render_matches_original(void) {
  boot();
  for (const Case &c : CASES) {
    fillTileRam(c.seed);
    TEST_ASSERT_EQUAL_HEX32(c.crc, renderCrc(c.scrollx, c.scrolly));
  }
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_render_matches_original);
#endif
  return UNITY_END();
}
