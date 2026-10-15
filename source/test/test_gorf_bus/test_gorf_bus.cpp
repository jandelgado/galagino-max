#include <unity.h>
#include "../../src/machines/gorf/astrocade_bus.h"
using namespace astrocade;
struct Mem { uint8_t m[0x10000] = {}; FuncGen fg;
  uint8_t rd(uint16_t a) { return m[a]; }
  void wr(uint16_t a, uint8_t d) {
    if (a < 0x4000) { uint8_t o; if (fg.apply(d, m[0x4000 + a], &o)) m[0x4000 + a] = o; }
    else m[a] = d; } };
void setUp() {} void tearDown() {}
static void test_plain_write() { FuncGen f; uint8_t o; TEST_ASSERT_TRUE(f.apply(0xa5, 0, &o)); TEST_ASSERT_EQUAL_HEX8(0xa5, o); }
static void test_shift_carries_prev() { FuncGen f; f.setControl(0x01); uint8_t o;
  f.apply(0xff, 0, &o); TEST_ASSERT_EQUAL_HEX8(0x3f, o); f.apply(0x00, 0, &o); TEST_ASSERT_EQUAL_HEX8(0xc0, o); }
static void test_expand_nibbles() { FuncGen f; f.setExpand(0x0e /*0->2, 1->3*/); f.setControl(0x08); uint8_t o;
  f.apply(0xa0, 0, &o); TEST_ASSERT_EQUAL_HEX8(0xee, o);  // count toggles to 1 first: high nibble 1010 -> 3,2,3,2
  f.apply(0xa0, 0, &o); TEST_ASSERT_EQUAL_HEX8(0xaa, o); } // then low nibble 0000 -> 2,2,2,2
static void test_flop() { FuncGen f; f.setControl(0x40); uint8_t o; f.apply(0x1b, 0, &o); TEST_ASSERT_EQUAL_HEX8(0xe4, o); }
static void test_xor_and_intercept() { FuncGen f; f.setControl(0x20); uint8_t o; f.apply(0xc0, 0xc3, &o);
  TEST_ASSERT_EQUAL_HEX8(0x03, o); TEST_ASSERT_EQUAL_HEX8(0x11, f.intercept & 0x11); }
static void test_rotate_needs_8_writes() { FuncGen f; f.setControl(0x04); uint8_t o; int stored = 0;
  for (int i = 0; i < 8; i++) stored += f.apply(0xc0 >> (2 * (i & 3)), 0, &o); TEST_ASSERT_EQUAL_INT(4, stored); }
static void test_blit_copies_rows_with_skip() { static Mem m; Pattern p;
  for (int i = 0; i < 4; i++) m.m[0x8000 + i] = 0x11 * (i + 1);
  p.write(0, 0x00, m); p.write(1, 0x80, m); p.write(2, 0x24 /*src inc, dest inc*/, m); p.write(3, 79, m);
  p.write(4, 0x40, m); p.write(5, 1, m); int cyc = p.write(6, 1, m);   // 2 bytes x 2 rows
  // dest hi write adds skip: start 79; last byte of a row does not advance dest, so row 2 = 80 + 79
  TEST_ASSERT_EQUAL_HEX8(0x11, m.m[0x4000 + 79]); TEST_ASSERT_EQUAL_HEX8(0x22, m.m[0x4000 + 80]);
  TEST_ASSERT_EQUAL_HEX8(0x33, m.m[0x4000 + 159]); TEST_ASSERT_EQUAL_HEX8(0x44, m.m[0x4000 + 160]);
  TEST_ASSERT_EQUAL_INT(16, cyc); }
static void test_blit_to_funcgen_area() { static Mem m; Pattern p; m.fg.setControl(0x20); m.m[0x4000] = 0xff; m.m[0x8000] = 0x0f;
  p.write(0, 0, m); p.write(1, 0x80, m); p.write(2, 0x24, m); p.write(3, 0, m); p.write(4, 0x00, m); p.write(5, 0, m); p.write(6, 0, m);
  TEST_ASSERT_EQUAL_HEX8(0xf0, m.m[0x4000]); }   // XOR via funcgen, Review Focus 2
int main() { UNITY_BEGIN(); RUN_TEST(test_plain_write); RUN_TEST(test_shift_carries_prev); RUN_TEST(test_expand_nibbles);
  RUN_TEST(test_flop); RUN_TEST(test_xor_and_intercept); RUN_TEST(test_rotate_needs_8_writes);
  RUN_TEST(test_blit_copies_rows_with_skip); RUN_TEST(test_blit_to_funcgen_area); return UNITY_END(); }
