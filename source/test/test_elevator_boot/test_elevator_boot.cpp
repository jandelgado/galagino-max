#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots the Elevator Action bootleg ROM (elevatorb) on the host through the
// machine class: both Z80s, sound latch/NMI, gfx ROM copy into character
// RAM. ROM headers come from romconv (./convert.sh elevator); without them
// the test is skipped. Arduino.h / FastLED.h / esp_attr.h are host stubs.
#if __has_include("../../src/machines/elevator/elevator_main_rom.h")
#define HAVE_ROM 1
#define ENABLE_ELEVATOR
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/elevator/elevator.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static elevator *machine;
static uint32_t frame;
static uint32_t latchWrites, soundLatchReads;

// opcode fetches at the idle loop entry per CPU
static const uint16_t IDLE_PC[2] = {0x73f2, 0x0164};
static uint32_t idleFetches[2];

extern "C" {
unsigned char OpZ80_INL(unsigned short a) {
  idleFetches[machine->current_cpu] += a == IDLE_PC[machine->current_cpu];
  return machine->opZ80(a);
}
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) {
  if (machine->current_cpu == elevator::SOUND_CPU && (a & 0xf803) == 0x5000) {
    soundLatchReads++;
  }
  return machine->rdZ80(a);
}
void WrZ80(unsigned short a, unsigned char v) {
  if (machine->current_cpu == elevator::MAIN_CPU && (a & 0xff0f) == 0xd50b) {
    latchWrites++;
  }
  machine->wrZ80(a, v);
}
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static const uint32_t ATTRACT_FRAMES = 600;

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  machine = new elevator();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void runTo(uint32_t f) {
  while (frame < f) {
    machine->run_frame();
    frame++;
  }
}

static uint32_t nonZero(const uint8_t *p, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += p[i] != 0;
  }
  return c;
}

// attract mode (MAME frame 600: 320 tilemap bytes): title drawn, gfx copied to character RAM, palette set,
// the sound CPU takes commands from the latch
static void test_boots_to_attract(void) {
  boot();
  runTo(ATTRACT_FRAMES);
  const uint32_t tileBytes = nonZero(&memory[elevator::VRAM_OFS + 0x400], 0xc00);
  const uint32_t charBytes = nonZero(machine->charram, elevator::CHARRAM_SIZE);
  const uint32_t palBytes = nonZero(&memory[elevator::PAL_OFS], elevator::PAL_SIZE);
  printf("tilemaps %u, charram %u, palette %u, latch writes %u, latch reads %u\n", tileBytes,
         charBytes, palBytes, latchWrites, soundLatchReads);
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_GREATER_THAN_UINT32(300, tileBytes);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, charBytes);
  TEST_ASSERT_GREATER_THAN_UINT32(10, palBytes);
  TEST_ASSERT_GREATER_THAN_UINT32(0, latchWrites);
  TEST_ASSERT_GREATER_THAN_UINT32(0, soundLatchReads);
}

// idle loops (main: wait for the vblank IRQ, sound: wait for IRQ/NMI) are
// skipped, not stepped: a few fetches per frame instead of ~1700 / ~900
static void test_idle_loops_skipped(void) {
  boot();
  runTo(ATTRACT_FRAMES);
  idleFetches[0] = idleFetches[1] = 0;
  const uint32_t frames = 600;
  runTo(ATTRACT_FRAMES + frames);
  printf("idle fetches per frame: main %u, sound %u\n", idleFetches[0] / frames, idleFetches[1] / frames);
  TEST_ASSERT_LESS_THAN_UINT32(10 * frames, idleFetches[0]);
  TEST_ASSERT_LESS_THAN_UINT32(10 * frames, idleFetches[1]);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boots_to_attract);
  RUN_TEST(test_idle_loops_skipped);
#endif
  return UNITY_END();
}
