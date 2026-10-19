#include <string.h>
#include <unity.h>
#include "../../src/machines/williams/williams_blitter.h"

struct Bus {
  uint8_t mem[0x10000];
  uint8_t read(uint16_t a) { return mem[a]; }
  uint8_t readVram(uint16_t a) { return mem[a]; }
  void write(uint16_t a, uint8_t v) { mem[a] = v; }
};

// B1Board's bus: writes at or above 0xc000 (I/O, CMOS) are dropped
struct RamOnlyBus : Bus {
  void write(uint16_t a, uint8_t v) {
    if (a < 0xc000) {
      mem[a] = v;
    }
  }
};

static Bus bus;
static Sc1Blitter blt;

// registers: 0 control (starts), 1 solid, 2/3 src, 4/5 dst, 6 width, 7 height
template <class B>
static uint32_t blit(B &b, uint8_t control, uint16_t src, uint16_t dst, int w, int h,
                     uint8_t solid = 0) {
  blt.write(1, solid, b);
  blt.write(2, src >> 8, b);
  blt.write(3, src & 0xff, b);
  blt.write(4, dst >> 8, b);
  blt.write(5, dst & 0xff, b);
  blt.write(6, w ^ 4, b);  // SC1 XORs the size with 4
  blt.write(7, h ^ 4, b);
  return blt.write(0, control, b);
}

void setUp(void) {
  memset(&bus, 0, sizeof(bus));
  blt.reset();
}
void tearDown(void) {}

void test_linear_to_screen_layout(void) {
  const uint8_t img[4] = { 0x12, 0x34, 0x56, 0x78 };  // 2 x 2 bytes, linear
  memcpy(bus.mem + 0xd000, img, 4);
  const uint32_t stall = blit(bus, 0x02, 0xd000, 0x1010, 2, 2);  // dst stride 256
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.mem[0x1010]);
  TEST_ASSERT_EQUAL_HEX8(0x34, bus.mem[0x1110]);
  TEST_ASSERT_EQUAL_HEX8(0x56, bus.mem[0x1011]);
  TEST_ASSERT_EQUAL_HEX8(0x78, bus.mem[0x1111]);
  TEST_ASSERT_EQUAL(((4 + 2 * (8 + 3)) + 3) / 4, stall);  // 8 accesses, fast
}

void test_foreground_only_keeps_background(void) {
  bus.mem[0xd000] = 0x05;  // even pixel transparent, odd pixel 5
  bus.mem[0x2000] = 0xab;
  blit(bus, 0x08, 0xd000, 0x2000, 1, 1);
  TEST_ASSERT_EQUAL_HEX8(0xa5, bus.mem[0x2000]);
}

void test_solid_color(void) {
  bus.mem[0xd000] = 0x11;
  blit(bus, 0x18, 0xd000, 0x2000, 1, 1, 0xcc);
  TEST_ASSERT_EQUAL_HEX8(0xcc, bus.mem[0x2000]);
}

void test_shift_right_one_pixel(void) {
  bus.mem[0xd000] = 0x12;
  bus.mem[0xd001] = 0x34;
  blit(bus, 0x20, 0xd000, 0x2000, 2, 1);
  TEST_ASSERT_EQUAL_HEX8(0x01, bus.mem[0x2000]);
  TEST_ASSERT_EQUAL_HEX8(0x23, bus.mem[0x2001]);
}

void test_zero_size_blits_one_byte(void) {
  bus.mem[0xd000] = 0x77;
  blt.write(2, 0xd0, bus);
  blt.write(3, 0x00, bus);
  blt.write(4, 0x20, bus);
  blt.write(5, 0x00, bus);
  blt.write(6, 4, bus);  // 4 ^ 4 = 0 -> 1
  blt.write(7, 4, bus);
  blt.write(0, 0x00, bus);
  TEST_ASSERT_EQUAL_HEX8(0x77, bus.mem[0x2000]);
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.mem[0x2001]);
}

void test_high_destination_leaves_ram(void) {
  static RamOnlyBus ramBus;
  memset(&ramBus, 0, sizeof(ramBus));
  memset(ramBus.mem + 0xd000, 0x5a, 16);
  blit(ramBus, 0x00, 0xd000, 0xcc00, 4, 4);  // linear 16 bytes onto CMOS
  for (int a = 0; a < 0xc000; a++) {
    TEST_ASSERT_EQUAL_HEX8(0, ramBus.mem[a]);
  }
  TEST_ASSERT_EQUAL_HEX8(0, ramBus.mem[0xcc00]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_linear_to_screen_layout);
  RUN_TEST(test_foreground_only_keeps_background);
  RUN_TEST(test_solid_color);
  RUN_TEST(test_shift_right_one_pixel);
  RUN_TEST(test_zero_size_blits_one_byte);
  RUN_TEST(test_high_destination_leaves_ram);
  return UNITY_END();
}
