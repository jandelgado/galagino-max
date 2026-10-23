#include <unity.h>

// I8039Dac: i8039 stepped in cycle debt, P1 sampled at 24 kHz into a ring,
// popped by render() on the audio side.
#include "../../src/cpus/i8048/i8048.c"
#include "../../src/emulation/i8039_dac.cpp"

void setUp(void) {}
void tearDown(void) {}

static I8039Dac *dac;

extern "C" {
void i8048_port_write(i8048_state_S *, unsigned char p, unsigned char v) {
  if (p == 1) {
    dac->port1(v);
  }
}
unsigned char i8048_port_read(i8048_state_S *, unsigned char) { return 0xff; }
unsigned char i8048_rom_read(i8048_state_S *s, unsigned short a) { return s->rom_direct[a & 0x0fff]; }
unsigned char i8048_xdm_read(i8048_state_S *, unsigned char) { return 0; }
void i8048_xdm_write(i8048_state_S *, unsigned char, unsigned char) {}
}

// INC A / OUTL P1,A / JMP 0: the DAC counts up, one step per 4 cycles
static uint8_t rom[0x1000] = {0x17, 0x39, 0x04, 0x00};

static void test_render_empty_holds_center(void) {
  I8039Dac d(8000000 / 15, 1);
  dac = &d;
  d.reset(rom);
  int16_t out[8] = {0};
  d.render(out, 8);
  for (int i = 0; i < 8; i++) {
    TEST_ASSERT_EQUAL(0, out[i]);
  }
}

static void test_samples_follow_port1(void) {
  I8039Dac d(8000000 / 15, 1);
  dac = &d;
  d.reset(rom);
  d.run(8000000 / 15 / 60);   // one frame: 400 samples
  TEST_ASSERT_INT_WITHIN(1, 400, d.queued());
  int16_t out[400] = {0};
  d.render(out, 400);
  TEST_ASSERT_EQUAL(0, d.queued());
  TEST_ASSERT_NOT_EQUAL(out[0], out[1]);
}

static void test_ring_overrun_drops(void) {
  I8039Dac d(8000000 / 15, 1);
  dac = &d;
  d.reset(rom);
  for (int f = 0; f < 10; f++) {
    d.run(8000000 / 15 / 60);
  }
  TEST_ASSERT_EQUAL(I8039Dac::RING - 1, d.queued());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_render_empty_holds_center);
  RUN_TEST(test_samples_follow_port1);
  RUN_TEST(test_ring_overrun_drops);
  return UNITY_END();
}
