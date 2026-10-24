#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots the Jungle Hunt ROM (jungleh) on the host through the elevator
// machine class (Taito SJ board without MCU): both Z80s, gfx ROM copy into
// character RAM, coin and start. ROM headers come from romconv
// (./convert.sh jungleh); without them the test is skipped.
#if __has_include("../../src/machines/jungleh/jungleh_main_rom.h")
#define HAVE_ROM 1
#define ENABLE_JUNGLEH
#define ELEVATOR_Z80_TABLES_INCLUDED
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/elevator/elevator.cpp"
#include "../../src/machines/jungleh/jungleh.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static jungleh *machine;
static uint32_t frame;

// opcode fetches at the idle loop entry per CPU
static const uint16_t IDLE_PC[2] = {0x0d11, 0x00cb};
static uint32_t idleFetches[2];

extern "C" {
unsigned char OpZ80_INL(unsigned short a) {
  idleFetches[machine->current_cpu] += a == IDLE_PC[machine->current_cpu];
  return machine->opZ80(a);
}
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

// credit counter (MAME jungleh: $8169 0 -> 1 on a coin, 0 after start)
static const uint16_t CREDITS = 0x0169;
static const uint32_t COIN_FRAME = 1000, START_FRAME = 1200, PLAY_FRAME = 1300;
static uint32_t dacSamples;   // DAC ring samples after start that are not 0

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  machine = new jungleh();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void runTo(uint32_t f) {
  while (frame < f) {
    keys = 0;
    if (frame >= COIN_FRAME && frame < COIN_FRAME + 5) {
      keys = BUTTON_COIN;
    }
    if (frame >= START_FRAME && frame < START_FRAME + 5) {
      keys = BUTTON_START;
    }
    if (frame > PLAY_FRAME && frame % 60 < 5) {
      keys = BUTTON_FIRE;   // jump now and then, as the MAME probe did
    }
    machine->run_frame();
    frame++;
    // drain the DAC ring as renderAudio() would
    for (uint16_t i = machine->dac_rd; i != machine->dac_wr; i = (i + 1) & (elevator::DAC_RING - 1)) {
      dacSamples += frame > START_FRAME && machine->dac_ring[i] != 0;
    }
    machine->dac_rd = machine->dac_wr;
  }
  keys = 0;
}

static uint32_t nonZero(const uint8_t *p, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += p[i] != 0;
  }
  return c;
}

// attract: tiles drawn, gfx copied to character RAM, palette set
static void test_boots_to_attract(void) {
  boot();
  runTo(600);
  const uint32_t tileBytes = nonZero(&memory[elevator::VRAM_OFS + 0x400], 0xc00);
  const uint32_t charBytes = nonZero(machine->charram, elevator::CHARRAM_SIZE);
  const uint32_t palBytes = nonZero(&memory[elevator::PAL_OFS], elevator::PAL_SIZE);
  printf("tilemaps %u, charram %u, palette %u\n", tileBytes, charBytes, palBytes);
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_GREATER_THAN_UINT32(100, tileBytes);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, charBytes);
  TEST_ASSERT_GREATER_THAN_UINT32(10, palBytes);
}

static void test_coin_and_start(void) {
  boot();
  runTo(COIN_FRAME - 10);
  TEST_ASSERT_EQUAL_UINT8(0, memory[elevator::RAM_OFS + CREDITS]);
  runTo(COIN_FRAME + 100);
  TEST_ASSERT_EQUAL_UINT8(1, memory[elevator::RAM_OFS + CREDITS]);
  runTo(START_FRAME + 500);
  TEST_ASSERT_EQUAL_UINT8(0, memory[elevator::RAM_OFS + CREDITS]);
}

// idle loops (main: wait for the vblank IRQ, sound: wait for a command)
// are skipped: a few fetches per frame, not thousands
static void test_idle_loops_skipped(void) {
  boot();
  runTo(600);
  idleFetches[0] = idleFetches[1] = 0;
  const uint32_t frames = 300;
  runTo(600 + frames);
  printf("idle fetches per frame: main %u, sound %u\n", idleFetches[0] / frames,
         idleFetches[1] / frames);
  TEST_ASSERT_LESS_THAN_UINT32(10 * frames, idleFetches[0]);
  TEST_ASSERT_LESS_THAN_UINT32(10 * frames, idleFetches[1]);
}

// the Tarzan yell goes through the DAC on AY#1 ports A (data) and B
// (volume); the idle loop keeps it at volume 0. MAME (FIRE every second
// from frame 1300): DAC volume on from frame 1701
static void test_tarzan_yell_on_dac(void) {
  boot();
  runTo(START_FRAME + 1200);
  printf("DAC samples not 0: %u\n", dacSamples);
  TEST_ASSERT_GREATER_THAN_UINT32(24000, dacSamples);   // over 1 s of yell
}

// IN0 (d408): 8-way stick, FIRE = button 1 even with UP held (no jump key)
static void test_inputs_map(void) {
  boot();
  static const struct { unsigned int key; uint8_t in0; } MAP[] = {
      {BUTTON_LEFT, 0xfe}, {BUTTON_RIGHT, 0xfd}, {BUTTON_DOWN, 0xfb}, {BUTTON_UP, 0xf7},
      {BUTTON_FIRE, 0xef}, {BUTTON_FIRE | BUTTON_UP, 0xe7}, {BUTTON_B, 0xff}};
  for (const auto &m : MAP) {
    keys = m.key;
    TEST_ASSERT_EQUAL_HEX8(m.in0, machine->in0());
  }
  keys = 0;
  TEST_ASSERT_EQUAL_HEX8(0x3f, machine->rdIo(0xa));   // DSW1: MAME defaults, upright
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boots_to_attract);
  RUN_TEST(test_idle_loops_skipped);
  RUN_TEST(test_coin_and_start);
  RUN_TEST(test_inputs_map);
  RUN_TEST(test_tarzan_yell_on_dac);
#endif
  return UNITY_END();
}
