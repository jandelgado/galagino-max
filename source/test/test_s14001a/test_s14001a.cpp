#include <unity.h>
#include <string.h>

#include "../../src/emulation/s14001a.cpp"

static uint8_t rom[4096];
static S14001A chip;

void setUp(void) { memset(rom, 0, sizeof(rom)); }
void tearDown(void) {}

// word 0: control word pointer rom[0..1] -> CWAR 0x010; control word at
// 0x010: DAR MSB (delta data at 0x080), then ctrl (stop|voiced|silence|len|repeat)
static void build_word0(uint8_t ctrl) {
  rom[0] = 0x01;
  rom[1] = 0x00;
  rom[0x10] = 0x08;
  rom[0x11] = ctrl;
  for (int i = 0x80; i < 0x400; i++) {
    rom[i] = 0xff; // every delta "add 3"
  }
}

static int run_until_idle(int limit) {
  for (int i = 0; i < limit; i++) {
    chip.clock();
    if (!chip.busy()) {
      return i;
    }
  }
  return -1;
}

static void test_idle_outputs_silence(void) {
  chip.reset(rom, sizeof(rom));
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL_UINT8(7, chip.clock());
  }
  TEST_ASSERT_FALSE(chip.busy());
}

static void test_busy_after_start(void) {
  build_word0(0x80); // stop, unvoiced, length 0 (longest: counts up to 0x7f)
  chip.reset(rom, sizeof(rom));
  chip.data_w(0);
  chip.start();
  TEST_ASSERT_FALSE(chip.busy()); // MAME: busy rises on the next clock
  chip.clock();
  chip.clock();
  TEST_ASSERT_TRUE(chip.busy());
  TEST_ASSERT_TRUE(run_until_idle(100000) > 0);
}

static void test_unvoiced_add3_ramps_to_15(void) {
  build_word0(0x9f); // stop, unvoiced, shortest length
  chip.reset(rom, sizeof(rom));
  chip.data_w(0);
  chip.start();
  uint8_t peak = 0;
  for (int i = 0; i < 4000 && (i < 4 || chip.busy()); i++) {
    const uint8_t o = chip.clock();
    if (o > peak) {
      peak = o;
    }
  }
  TEST_ASSERT_EQUAL_UINT8(15, peak);
}

// restarting mid-word re-enters WORDWAIT and still finishes
static void test_restart_while_speaking(void) {
  build_word0(0xfc);
  chip.reset(rom, sizeof(rom));
  chip.data_w(0);
  chip.start();
  for (int i = 0; i < 50; i++) {
    chip.clock();
  }
  chip.data_w(0);
  chip.start();
  TEST_ASSERT_TRUE(run_until_idle(200000) > 0);
}

// word 5: pointer at ((5 & 0x3c) >> 2) << 3 | ((5 & 3) << 3) >> 2 = 0x0a
static void test_word_address_decode(void) {
  rom[0x0a] = 0x02; // CWAR 0x020
  rom[0x0b] = 0x00;
  rom[0x20] = 0x10; // delta data at 0x100
  rom[0x21] = 0x9f; // stop, shortest length
  for (int i = 0x100; i < 0x180; i++) {
    rom[i] = 0xff;
  }
  chip.reset(rom, sizeof(rom));
  chip.data_w(5);
  chip.start();
  uint8_t peak = 0;
  for (int i = 0; i < 2000; i++) {
    const uint8_t o = chip.clock();
    if (o > peak) {
      peak = o;
    }
  }
  TEST_ASSERT_TRUE(peak > 7); // played the add-3 deltas at 0x100
  TEST_ASSERT_FALSE(chip.busy());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_outputs_silence);
  RUN_TEST(test_busy_after_start);
  RUN_TEST(test_unvoiced_add3_ramps_to_15);
  RUN_TEST(test_restart_while_speaking);
  RUN_TEST(test_word_address_decode);
  return UNITY_END();
}
