#include <string.h>
#include <unity.h>
#if __has_include("../../src/machines/defender/defender_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/asset.h"
#include "../../src/machines/defender/defender_rom.h"
#endif
#include "../../src/cpus/m6803/m6803.c"
#include "../../src/machines/williams/williams_sound.cpp"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t FRAME_CYCLES = 16640;  // main 6809 cycles per frame
static uint8_t ram[256];

// A command that arrives while I is masked must still be taken after CLI.
// ROM at 0xff00:
//   ff00 SEI; LDAA #$07; STAA $0403 (CRB: OR select, rising CB1, IRQ on)
//   ff06 LDX #$0100; DEX; BNE ff09 (~2300 main cycles masked); CLI; BRA *
//   ff10 INC $0010; RTI          (handler never acks: re-enters, fine)
void test_command_during_masked_irq(void) {
  static uint8_t rom[256];
  const uint8_t code[] = { 0x0f, 0x86, 0x07, 0xb7, 0x04, 0x03, 0xce, 0x01, 0x00,
                           0x09, 0x26, 0xfd, 0x0e, 0x20, 0xfe };
  const uint8_t handler[] = { 0x7c, 0x00, 0x10, 0x3b };
  memcpy(rom, code, sizeof(code));
  memcpy(rom + 0x10, handler, sizeof(handler));
  rom[0xf8] = 0xff;  // IRQ vector 0xff10
  rom[0xf9] = 0x10;
  rom[0xfe] = 0xff;  // reset vector 0xff00
  rom[0xff] = 0x00;
  WilliamsSound snd;
  snd.attach(rom, 0xff00, ram);
  snd.reset();
  snd.run(50);         // CRB set up, I masked, in the delay loop
  snd.command(0x01);
  snd.run(100);
  TEST_ASSERT_EQUAL(0, ram[0x10]);
  snd.run(5000);       // past CLI
  TEST_ASSERT_TRUE(ram[0x10] > 0);
}

#ifdef HAVE_ROM
static WilliamsSound &boot(WilliamsSound &snd) {
  static bool once = false;
  if (!once) {
    Arena::init();
    once = true;
  }
  static Asset<unsigned char, COMPRESSED> rom(defender_rom_sound_blob);
  snd.attach(rom.data(), 0xf800, ram);
  snd.reset();
  return snd;
}

// frames in which the 6808 changed the DAC
static int countChanges(WilliamsSound &snd, int frames) {
  int changes = 0;
  uint8_t last = snd.dac();
  for (int f = 0; f < frames; f++) {
    snd.run(FRAME_CYCLES);
    while (snd.sample() != WilliamsSound::EMPTY) {
    }
    if (snd.dac() != last) {
      changes++;
      last = snd.dac();
    }
  }
  return changes;
}

void test_silent_without_command(void) {
  WilliamsSound snd;
  boot(snd);
  snd.command(0xff);  // idle
  snd.run(FRAME_CYCLES * 10);  // boot code may set the DAC once
  TEST_ASSERT_EQUAL(0, countChanges(snd, 30));
}

void test_command_makes_sound(void) {
  WilliamsSound snd;
  boot(snd);
  snd.command(0xff);
  snd.run(FRAME_CYCLES);
  snd.command(0x01);  // any non-idle command
  TEST_ASSERT_TRUE(countChanges(snd, 30) > 0);
}

void test_ring_produces_400_samples_per_frame(void) {
  WilliamsSound snd;
  boot(snd);
  snd.run(FRAME_CYCLES);
  int n = 0;
  while (snd.sample() != WilliamsSound::EMPTY && n < WilliamsSound::RING) {
    n++;
  }
  TEST_ASSERT_INT_WITHIN(3, 399, n);  // 24000 / 60.1
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_command_during_masked_irq);
#ifdef HAVE_ROM
  RUN_TEST(test_silent_without_command);
  RUN_TEST(test_command_makes_sound);
  RUN_TEST(test_ring_produces_400_samples_per_frame);
#endif
  return UNITY_END();
}
