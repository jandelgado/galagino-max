#include <string.h>
#include <unity.h>

#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/dvg.cpp"

using vec2d::Line;

static uint8_t ram[0x800], rom[0x800];
static Line out[128];

// asteroids layout: ROM at DVG words 0x800-0xbff, no glyphs
static dvg::Memory mem() {
  return dvg::Memory{ram, rom, 0x800, 0x400, 0, 0};
}

void setUp(void) {
  memset(ram, 0, sizeof(ram));
  memset(rom, 0, sizeof(rom));
  memset(out, 0x55, sizeof(out));
}
void tearDown(void) {}

static void put(uint8_t *mem, uint16_t word, uint16_t w) {
  mem[word * 2] = w & 0xff;
  mem[word * 2 + 1] = w >> 8;
}

static const uint16_t HALT = 0xb000;

static void assert_line(const Line &l, int x0, int y0, int x1, int y1, int color) {
  TEST_ASSERT_EQUAL(x0, l.x0);
  TEST_ASSERT_EQUAL(y0, l.y0);
  TEST_ASSERT_EQUAL(x1, l.x1);
  TEST_ASSERT_EQUAL(y1, l.y1);
  TEST_ASSERT_EQUAL(color, l.color);
}

static void test_labs_vctr(void) {
  put(ram, 0, 0xa000 | 200);         // LABS y=200
  put(ram, 1, 0x0000 | 100);         //      x=100, gscale 0
  put(ram, 2, 0x9000 | 0x400 | 128); // VCTR scale 9, dy=-128
  put(ram, 3, 0xc000 | 256);         //      intensity 12, dx=+256
  put(ram, 4, 0x7000 | 128);         // VCTR scale 7: length / 4, dy=+32
  put(ram, 5, 0x7000 | 0x400 | 256); //      intensity 7, dx=-64
  put(ram, 6, HALT);
  TEST_ASSERT_EQUAL(2, dvg::decode(mem(), out, 128));
  assert_line(out[0], 100, 200, 356, 72, 12);
  assert_line(out[1], 356, 72, 292, 104, 7);
}

// MAME's rate multiplier rounds half up: 3 * 256 / 1024 = 0.75 -> 1,
// 2 * 256 / 1024 = 0.5 -> 1
static void test_length_rounds(void) {
  put(ram, 0, 0xa000 | 100);
  put(ram, 1, 100);
  put(ram, 2, 0x7000 | 3);
  put(ram, 3, 0xf000 | 2);
  put(ram, 4, HALT);
  TEST_ASSERT_EQUAL(1, dvg::decode(mem(), out, 128));
  assert_line(out[0], 100, 100, 101, 101, 15);
}

static void test_intensity_zero_moves(void) {
  put(ram, 0, 0xa000 | 10);
  put(ram, 1, 10);
  put(ram, 2, 0x9000 | 50);  // move dy=+50, intensity 0
  put(ram, 3, 0x0000 | 20);  //      dx=+20
  put(ram, 4, 0x9000);       // dot, intensity 15
  put(ram, 5, 0xf000);
  put(ram, 6, HALT);
  TEST_ASSERT_EQUAL(1, dvg::decode(mem(), out, 128));
  assert_line(out[0], 30, 60, 30, 60, 15);
}

static void test_svec(void) {
  // gscale 1. SVEC word: 15-12 F, 11 scale bit, 10 dy sign, 9-8 dy,
  // 7-4 intensity, 3 scale bit, 2 dx sign, 1-0 dx.
  // dvy b11=0, dvx b11=0: scale 1 + 2 = 3, len = mag * 16 >> 10 (0x300 -> 12)
  put(ram, 0, 0xa000 | 500);
  put(ram, 1, 0x1000 | 500);
  put(ram, 2, 0xf000 | (3 << 8) | (9 << 4) | 0x4 | 2); // dy=+0x300 dx=-0x200
  // dvy b11=1, dvx b11=1: scale 1 + 5 = 6, len = mag * 128 >> 10
  put(ram, 3, 0xf000 | 0x800 | 0x400 | (1 << 8) | (5 << 4) | 0x8 | 1);
  put(ram, 4, HALT);
  TEST_ASSERT_EQUAL(2, dvg::decode(mem(), out, 128));
  assert_line(out[0], 500, 500, 500 - 8, 500 + 12, 9);
  assert_line(out[1], 492, 512, 492 + 32, 512 - 32, 5);
}

static void test_jsrl_rtsl_jmpl(void) {
  put(ram, 0, 0xa000 | 0);
  put(ram, 1, 0);
  put(ram, 2, 0xc800);       // JSRL rom word 0
  put(ram, 3, 0xe010);       // JMPL 0x010
  put(ram, 0x10, 0xc800);    // JSRL rom word 0 again
  put(ram, 0x11, HALT);
  put(rom, 0, 0x9000 | 10);  // VCTR dy=+10
  put(rom, 1, 0xf000);       //      dx=0, intensity 15
  put(rom, 2, 0xd000);       // RTSL
  TEST_ASSERT_EQUAL(2, dvg::decode(mem(), out, 128));
  assert_line(out[0], 0, 0, 0, 10, 15);
  assert_line(out[1], 0, 10, 0, 20, 15);
}

static void test_runaway_terminates(void) {
  put(ram, 0, 0xe000); // JMPL to itself
  TEST_ASSERT_EQUAL(0, dvg::decode(mem(), out, 128));
  put(ram, 0, 0xc000); // JSRL to itself, stack wraps
  TEST_ASSERT_EQUAL(0, dvg::decode(mem(), out, 128));
}

static void test_capacity(void) {
  for (int i = 0; i < 100; i++) {
    put(ram, 2 * i, 0x9000 | 1);
    put(ram, 2 * i + 1, 0xf000);
  }
  put(ram, 200, HALT);
  TEST_ASSERT_EQUAL(10, dvg::decode(mem(), out, 10));
  TEST_ASSERT_EQUAL_HEX16(0x5555, (uint16_t)out[10].x0);
}

static void test_offscreen_signed(void) {
  put(ram, 0, 0xa000 | 0);
  put(ram, 1, 0xff0);       // x = 0xff0 = -16
  put(ram, 2, 0x9000);
  put(ram, 3, 0xf000 | 100);
  put(ram, 4, 0xa000 | 0);
  put(ram, 5, 1000);
  put(ram, 6, 0x9000);
  put(ram, 7, 0xf000 | 100); // 1000 + 100 stays 1100
  put(ram, 8, HALT);
  TEST_ASSERT_EQUAL(2, dvg::decode(mem(), out, 128));
  assert_line(out[0], -16, 0, 84, 0, 15);
  assert_line(out[1], 1000, 0, 1100, 0, 15);
}

static uint8_t rom6k[0x1800];

// llander layout: ROM at DVG words 0x400-0xfff
static void test_rom_window(void) {
  memset(rom6k, 0, sizeof(rom6k));
  put(ram, 0, 0xc400);              // JSRL 0x400 (first ROM word)
  put(ram, 1, 0xcffe);              // JSRL 0xffe (last ROM routine)
  put(ram, 2, HALT);
  put(rom6k, 0x000, 0x9000 | 16);   // VCTR scale 9, dy=+16
  put(rom6k, 0x001, 0x7000);        //      intensity 7, dx=0
  put(rom6k, 0x002, 0xd000);        // RTSL
  put(rom6k, 0xbfe, 0xf070 | 0x01); // SVEC intensity 7, dx>0
  put(rom6k, 0xbff, 0xd000);        // RTSL
  const dvg::Memory m{ram, rom6k, 0x400, 0xc00, 0, 0};
  TEST_ASSERT_EQUAL(2, dvg::decode(m, out, 128));
  assert_line(out[0], 0, 0, 0, 16, 7);
}

// glyph routine at 0x800 draws 8 units and advances 12; 0x810 is not a
// glyph
static void test_glyph_tagging(void) {
  put(rom, 0x00, 0x9000);            // VCTR scale 9, dy=0
  put(rom, 0x01, 0x7000 | 8);        //      intensity 7, dx=+8
  put(rom, 0x02, 0x9000);            // VCTR dy=0
  put(rom, 0x03, 0x0000 | 4);        //      intensity 0, dx=+4
  put(rom, 0x04, 0xd000);            // RTSL
  put(rom, 0x10, 0x9000 | 16);       // VCTR dy=+16
  put(rom, 0x11, 0x5000);            //      intensity 5
  put(rom, 0x12, 0xd000);            // RTSL
  put(ram, 0, 0xa000 | 200);         // LABS y=200
  put(ram, 1, 0x0000 | 100);         //      x=100
  put(ram, 2, 0xc800);               // glyph
  put(ram, 3, 0xc800);               // glyph, same run
  put(ram, 4, 0x9000 | 0x400 | 50);  // move dy=-50: ends the run
  put(ram, 5, 0x0000);
  put(ram, 6, 0xc800);               // glyph, new run
  put(ram, 7, 0xc810);               // not a glyph
  put(ram, 8, HALT);
  const dvg::Memory m{ram, rom, 0x800, 0x400, 0x800, 0x810};
  TEST_ASSERT_EQUAL(4, dvg::decode(m, out, 128));
  assert_line(out[0], 100, 200, 108, 200, 7 | dvg::GLYPH);
  assert_line(out[1], 112, 200, 120, 200, 7 | dvg::GLYPH);
  assert_line(out[2], 124, 150, 132, 150, 7 | dvg::GLYPH);
  assert_line(out[3], 136, 150, 136, 166, 5);
  TEST_ASSERT_EQUAL(100, out[0].dx);
  TEST_ASSERT_EQUAL(200, out[0].dy);
  TEST_ASSERT_EQUAL(100, out[1].dx);
  TEST_ASSERT_EQUAL(200, out[1].dy);
  TEST_ASSERT_EQUAL(124, out[2].dx);
  TEST_ASSERT_EQUAL(150, out[2].dy);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_labs_vctr);
  RUN_TEST(test_length_rounds);
  RUN_TEST(test_intensity_zero_moves);
  RUN_TEST(test_svec);
  RUN_TEST(test_jsrl_rtsl_jmpl);
  RUN_TEST(test_runaway_terminates);
  RUN_TEST(test_capacity);
  RUN_TEST(test_offscreen_signed);
  RUN_TEST(test_rom_window);
  RUN_TEST(test_glyph_tagging);
  return UNITY_END();
}
