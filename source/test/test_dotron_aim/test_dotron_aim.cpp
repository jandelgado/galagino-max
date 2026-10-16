// DotronAim: switches the game's attract-mode auto-aim on for the player by
// patching one branch in the decompressed main ROM.
#include <string.h>
#include <unity.h>

#include "../../src/emulation/input.h"
#include "../../src/machines/dotron/dotron_aim.h"

static uint8_t rom[0xe000];
static const uint16_t BRANCH = 0x5f69;
static const uint8_t JR_Z = 0x28, OFFSET = 0x2e, NOP = 0x00;

void setUp(void) {
  memset(rom, 0xff, sizeof(rom));
  rom[BRANCH] = JR_Z;
  rom[BRANCH + 1] = OFFSET;
}
void tearDown(void) {}

static void assertOriginal(void) {
  TEST_ASSERT_EQUAL_HEX8(JR_Z, rom[BRANCH]);
  TEST_ASSERT_EQUAL_HEX8(OFFSET, rom[BRANCH + 1]);
}

static void assertPatched(void) {
  TEST_ASSERT_EQUAL_HEX8(NOP, rom[BRANCH]);
  TEST_ASSERT_EQUAL_HEX8(NOP, rom[BRANCH + 1]);
}

// the boot ROM self test must see the original bytes
static void test_not_patched_before_boot(void) {
  DotronAim aim;
  aim.attach(rom);
  aim.update(0, DotronAim::BOOTING);
  assertOriginal();
}

static void test_patched_after_boot(void) {
  DotronAim aim;
  aim.attach(rom);
  aim.update(0, DotronAim::RUNNING);
  assertPatched();
}

// the demo aim path skips dial and elevation input: manual keys switch it off
static void test_manual_keys_restore(void) {
  static const unsigned int MANUAL[] = {BUTTON_L1, BUTTON_R1, BUTTON_X, BUTTON_Y};
  DotronAim aim;
  aim.attach(rom);
  for (unsigned int key : MANUAL) {
    aim.update(0, DotronAim::RUNNING);
    assertPatched();
    aim.update(key, DotronAim::RUNNING);
    assertOriginal();
  }
}

// stick, fire and deflect keep auto-aim on
static void test_play_keys_keep_patch(void) {
  DotronAim aim;
  aim.attach(rom);
  aim.update(BUTTON_LEFT | BUTTON_UP | BUTTON_FIRE | BUTTON_B, DotronAim::RUNNING);
  assertPatched();
}

static void test_restore(void) {
  DotronAim aim;
  aim.attach(rom);
  aim.update(0, DotronAim::RUNNING);
  aim.restore();
  assertOriginal();
}

// another ROM revision: leave it alone
static void test_unknown_rom_untouched(void) {
  rom[BRANCH + 1] = 0x30;
  DotronAim aim;
  aim.attach(rom);
  aim.update(0, DotronAim::RUNNING);
  TEST_ASSERT_EQUAL_HEX8(JR_Z, rom[BRANCH]);
  TEST_ASSERT_EQUAL_HEX8(0x30, rom[BRANCH + 1]);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_not_patched_before_boot);
  RUN_TEST(test_patched_after_boot);
  RUN_TEST(test_manual_keys_restore);
  RUN_TEST(test_play_keys_keep_patch);
  RUN_TEST(test_restore);
  RUN_TEST(test_unknown_rom_untouched);
  return UNITY_END();
}
