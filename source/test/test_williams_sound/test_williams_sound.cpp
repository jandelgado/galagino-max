#include <stdio.h>
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
#include "../../src/emulation/dc_block.h"

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

// The 24 kHz output is the DAC level averaged over each sample period, not
// the level at one instant: a DAC toggled far above 24 kHz must not alias.
// ROM at 0xff00 (PIA port A = DAC):
//   ff00 LDAA #$FF; STAA $0400 (DDRA out); LDAA #$04; STAA $0401 (CRA: ORA)
//   ff0a LDAA #$00; STAA $0400; LDAA #$FF; STAA $0400; BRA ff0a
// 18 cycle loop (50 kHz): 0x00 held 7 cycles, 0xff held 11, average 156.
void test_output_averages_dac(void) {
  static uint8_t rom[256];
  const uint8_t code[] = { 0x86, 0xff, 0xb7, 0x04, 0x00, 0x86, 0x04, 0xb7, 0x04, 0x01,
                           0x86, 0x00, 0xb7, 0x04, 0x00, 0x86, 0xff, 0xb7, 0x04, 0x00,
                           0x20, 0xf4 };
  memcpy(rom, code, sizeof(code));
  rom[0xfe] = 0xff;  // reset vector 0xff00
  rom[0xff] = 0x00;
  WilliamsSound snd;
  snd.attach(rom, 0xff00, ram);
  snd.reset();
  snd.run(FRAME_CYCLES);
  snd.sample();  // start-up: DAC still at reset level
  snd.sample();
  int n = 0;
  for (int v = snd.sample(); v != WilliamsSound::EMPTY; v = snd.sample(), n++) {
    TEST_ASSERT_INT_WITHIN(25, 156 - 0x80, v);
  }
  TEST_ASSERT_TRUE(n > 300);
}

// GWAVE delay loop (LDAA dir; DECA; BNE *-1; LDAA 0,X) runs as a whole,
// not opcode by opcode. ROM at 0xff00, PIA port A = DAC:
//   ff00 LDAA #$FF; STAA $0400; LDAA #$04; STAA $0401   DDRA out, CRA: ORA
//   ff0a LDAA #200; STAA $10; LDX #$ff30
//   ff11 LDAA $10; DECA; BNE ff13; LDAA 0,X; STAA $0400  wave byte to DAC
//   ff1b INX; CPX #$ff32; BNE ff11; LDX #$ff30; BRA ff11
//   ff30 00 ff                                            wave table
static const uint16_t GWAVE_DELAY_PC = 0xff13;
static uint32_t delayFetches = 0;
static m6803_read_fn_t soundRead = nullptr;
static uint8_t countingRead(uint16_t addr) {
  if (addr == GWAVE_DELAY_PC) {
    delayFetches++;
  }
  return soundRead(addr);
}

static void gwaveRom(uint8_t *rom) {
  const uint8_t code[] = { 0x86, 0xff, 0xb7, 0x04, 0x00, 0x86, 0x04, 0xb7, 0x04, 0x01,
                           0x86, 0xc8, 0x97, 0x10, 0xce, 0xff, 0x30,
                           0x96, 0x10, 0x4a, 0x26, 0xfd, 0xa6, 0x00, 0xb7, 0x04, 0x00,
                           0x08, 0x8c, 0xff, 0x32, 0x26, 0xf0, 0xce, 0xff, 0x30, 0x20, 0xeb };
  memcpy(rom, code, sizeof(code));
  rom[0x30] = 0x00;
  rom[0x31] = 0xff;
  rom[0xfe] = 0xff;  // reset vector 0xff00
  rom[0xff] = 0x00;
}

void test_gwave_delay_skipped(void) {
  static uint8_t rom[256];
  gwaveRom(rom);
  WilliamsSound snd;
  snd.attach(rom, 0xff00, ram);
  snd.reset();
  soundRead = m6803_ext_read_fn;
  m6803_ext_read_fn = countingRead;
  delayFetches = 0;
  snd.run(FRAME_CYCLES * 10);
  m6803_ext_read_fn = soundRead;
  // stepped: 48520; skipped: only the pass cut by each 1024 cycle chunk
  TEST_ASSERT_LESS_THAN_UINT32(500, delayFetches);
}

// Skipping keeps the output bit exact and the timing: 200 passes x 6
// cycles + 19 cycles per wave byte = 1219 cycles at 894886 Hz, square wave
// of 2438 cycles (367 Hz).
// Each half wave is ~32.7 output samples at 24 kHz, all 0x00 or 0xff.
void test_gwave_delay_timing(void) {
  static uint8_t rom[256];
  gwaveRom(rom);
  WilliamsSound snd;
  snd.attach(rom, 0xff00, ram);
  snd.reset();
  int high = 0, edges = 0, n = 0, last = 0;
  uint32_t h = 2166136261u;
  for (int f = 0; f < 10; f++) {
    snd.run(FRAME_CYCLES);
    for (int v = snd.sample(); v != WilliamsSound::EMPTY; v = snd.sample(), n++) {
      h = (h ^ (uint16_t)v) * 16777619u;
      if (n >= 100) {  // past start-up
        high += v > 0;
        edges += (v > 0) != (last > 0);
      }
      last = v;
    }
  }
  TEST_ASSERT_TRUE(n > 3000);
  TEST_ASSERT_INT_WITHIN(n / 50, (n - 100) / 2, high);
  TEST_ASSERT_INT_WITHIN(3, (n - 100) * 2 / 65, edges);
  TEST_ASSERT_EQUAL_HEX32(0x8136ec24, h);  // stepped opcode by opcode
}

#ifdef HAVE_ROM
static WilliamsSound &boot(WilliamsSound &snd) {
  static bool once = false;
  if (!once) {
    Arena::init();
    Arena::reserve(&defender_arena_plan);
    once = true;
  }
  static defender_rom_sound_asset rom(defender_rom_sound_blob);
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

// Every sound command, DAC_VOLUME scaled and DC blocked, stays inside the
// +-511 mix range. The DAC rests at 0 or 255 between sounds, so the next
// sound swings up to twice its amplitude around the old level.
void test_mix_fits_range(void) {
  static const int DAC_VOLUME = 2;  // williams.h
  static const int MIX_MAX = 511;   // Audio::valueToBuffer() input range
  static const int FRAMES = 90;
  for (int cmd = 0; cmd < 0x40; cmd++) {
    WilliamsSound snd;
    boot(snd);
    DcBlock dc;
    snd.command(0xff);
    snd.run(FRAME_CYCLES * 10);
    for (int f = 0; f < FRAMES; f++) {
      snd.command(f == 0 ? cmd : 0xff);
      snd.run(FRAME_CYCLES);
      for (int v = snd.sample(); v != WilliamsSound::EMPTY; v = snd.sample()) {
        const int out = dc.filter(v * DAC_VOLUME);
        if (out > MIX_MAX || out < -MIX_MAX) {
          char msg[48];
          snprintf(msg, sizeof(msg), "cmd %02x frame %d: %d", cmd, f, out);
          TEST_FAIL_MESSAGE(msg);
        }
      }
    }
  }
}
#endif

// Speech path: a 6808 loop toggles CB2 (CVSD clock) in manual mode with
// CA2 (data) held low: a run of 0 bits ramps the CVSD output up, so the
// ring average must rise above the idle DAC level (0).
// ROM at 0xff00:
//   ff00 LDAA #$34; STAA $0401   CRA: manual CA2 low, OR select
//   ff05 LDAA #$3c; STAA $0403   CRB: manual CB2 high
//   ff0a LDAA #$34; STAA $0403   CRB: manual CB2 low
//   ff0f BRA ff05
void test_speech_cvsd_from_pia(void) {
  static uint8_t rom[256];
  const uint8_t code[] = { 0x86, 0x34, 0xb7, 0x04, 0x01, 0x86, 0x3c, 0xb7, 0x04, 0x03,
                           0x86, 0x34, 0xb7, 0x04, 0x03, 0x20, 0xf4 };
  memcpy(rom, code, sizeof(code));
  rom[0xfe] = 0xff;
  rom[0xff] = 0x00;
  WilliamsSound snd;
  snd.attach(rom, 0xff00, ram, true);
  snd.reset();
  snd.run(FRAME_CYCLES);
  int maxv = 0;
  for (int v = snd.sample(); v != WilliamsSound::EMPTY; v = snd.sample()) {
    maxv = v > maxv ? v : maxv;
  }
  TEST_ASSERT_TRUE(maxv > 64);

  // without speech the same program is silent (DAC untouched)
  WilliamsSound quiet;
  quiet.attach(rom, 0xff00, ram);
  quiet.reset();
  quiet.run(FRAME_CYCLES);
  for (int v = quiet.sample(); v != WilliamsSound::EMPTY; v = quiet.sample()) {
    TEST_ASSERT_EQUAL_INT(0, v);
  }
}

#ifdef HAVE_ROM
// every command's output equals the one recorded before the GWAVE skip
void test_defender_output_hash(void) {
  uint32_t h = 2166136261u;
  for (int cmd = 0; cmd < 0x40; cmd++) {
    WilliamsSound snd;
    boot(snd);
    snd.command(0xff);
    snd.run(FRAME_CYCLES * 10);
    for (int f = 0; f < 30; f++) {
      snd.command(f == 0 ? cmd : 0xff);
      snd.run(FRAME_CYCLES);
      for (int v = snd.sample(); v != WilliamsSound::EMPTY; v = snd.sample()) {
        h = (h ^ (uint16_t)v) * 16777619u;
      }
    }
  }
  TEST_ASSERT_EQUAL_HEX32(0x5c72b9f5, h);  // stepped opcode by opcode
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_command_during_masked_irq);
  RUN_TEST(test_speech_cvsd_from_pia);
  RUN_TEST(test_output_averages_dac);
  RUN_TEST(test_gwave_delay_skipped);
  RUN_TEST(test_gwave_delay_timing);
#ifdef HAVE_ROM
  RUN_TEST(test_silent_without_command);
  RUN_TEST(test_command_makes_sound);
  RUN_TEST(test_ring_produces_400_samples_per_frame);
  RUN_TEST(test_mix_fits_range);
  RUN_TEST(test_defender_output_hash);
#endif
  return UNITY_END();
}
