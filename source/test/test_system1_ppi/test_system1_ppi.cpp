#include <unity.h>

#include "../../src/machines/system1/system1_ppi.h"

static Ppi8255 ppi;

static const uint8_t PORT_A = 0;
static const uint8_t PORT_B = 1;
static const uint8_t PORT_C = 2;
static const uint8_t CONTROL = 3;
static const uint8_t MODE_A2 = 0xc0;  // Star Jacker: group A mode 2, rest output
static const uint8_t MODE_ALL0 = 0x80;

void setUp(void) { ppi.reset(); }
void tearDown(void) {}

// reset and mode set leave PC7 (/OBF) high: no NMI at boot
static void test_mode_set_no_nmi(void) {
  ppi.write(CONTROL, MODE_A2);
  TEST_ASSERT_FALSE(ppi.takeNmi());
}

// mode 2: a sound command pulls /OBF low once, the latch read releases it
static void test_mode2_command_nmi(void) {
  ppi.write(CONTROL, MODE_A2);
  ppi.write(PORT_A, 0x90);
  TEST_ASSERT_TRUE(ppi.takeNmi());
  TEST_ASSERT_FALSE(ppi.takeNmi());

  ppi.write(PORT_A, 0x91);  // still unacked: no new edge
  TEST_ASSERT_FALSE(ppi.takeNmi());
  TEST_ASSERT_EQUAL_HEX8(0x91, ppi.ackA());

  ppi.write(PORT_A, 0x92);
  TEST_ASSERT_TRUE(ppi.takeNmi());
}

// mode 2: port C writes (mute bit) do not touch PC7
static void test_mode2_port_c_write(void) {
  ppi.write(CONTROL, MODE_A2);
  ppi.write(PORT_C, 0x01);
  ppi.write(PORT_C, 0x00);
  TEST_ASSERT_FALSE(ppi.takeNmi());
  TEST_ASSERT_EQUAL_HEX8(0x00, ppi.read(PORT_C) & 0x01);
  ppi.write(PORT_C, 0x01);
  TEST_ASSERT_EQUAL_HEX8(0x01, ppi.read(PORT_C) & 0x01);
}

// mode 0: PC7 is the port C latch, falling edge via write or bit reset
static void test_mode0_pc7_edge(void) {
  ppi.write(CONTROL, MODE_ALL0);
  ppi.write(PORT_C, 0x80);
  TEST_ASSERT_TRUE(ppi.takeNmi());  // mode set cleared the latch: 1 -> 0 at mode set
  ppi.write(PORT_C, 0x00);
  TEST_ASSERT_TRUE(ppi.takeNmi());
  ppi.write(CONTROL, 0x0f);  // set PC7
  TEST_ASSERT_FALSE(ppi.takeNmi());
  ppi.write(CONTROL, 0x0e);  // reset PC7
  TEST_ASSERT_TRUE(ppi.takeNmi());
}

// port B (video mode) reads back its latch: the game read-modify-writes it
static void test_port_b_readback(void) {
  ppi.write(CONTROL, MODE_A2);
  ppi.write(PORT_B, 0x10);
  TEST_ASSERT_EQUAL_HEX8(0x10, ppi.read(PORT_B));
  TEST_ASSERT_EQUAL_HEX8(0x10, ppi.portB());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_mode_set_no_nmi);
  RUN_TEST(test_mode2_command_nmi);
  RUN_TEST(test_mode2_port_c_write);
  RUN_TEST(test_mode0_pc7_edge);
  RUN_TEST(test_port_b_readback);
  return UNITY_END();
}
