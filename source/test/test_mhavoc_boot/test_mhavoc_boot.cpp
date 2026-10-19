#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots the Major Havoc ROM (mhavoc rev 3) on the host through the machine
// class: alpha and gamma 6502s, their latch handshake, AVG GO. ROM headers
// come from romconv (./convert.sh mhavoc); without them the test is
// skipped. Arduino.h / FastLED.h / esp_attr.h are host stubs.
#if __has_include("../../src/machines/mhavoc/mhavoc_rom.h")
#define HAVE_ROM 1
#define GAME_MHAVOC
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/vector2d.cpp"
#include "../../src/emulation/avg.cpp"
#include "../../src/emulation/dvg.cpp"
#include "../../src/emulation/pokey.cpp"
#include "../../src/machines/bwidow/bwidow_layout.cpp"
#define private public
#define protected public
#include "../../src/machines/dvgMachine.cpp"
#include "../../src/machines/mhavoc/mhavoc_layout.cpp"
#include "../../src/machines/mhavoc/mhavoc.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}
void Reset6502(M6502 *) {}

static mhavoc *machine;
static uint32_t frame;
static uint32_t gammaWrites, alphaReads; // alpha -> gamma bytes sent, read by gamma
static uint32_t gos;
static uint16_t maxLines;

static uint8_t (*alphaRead)(m6502_t *, uint16_t);
static void (*alphaWrite)(m6502_t *, uint16_t, uint8_t);
static uint8_t (*gammaRead)(m6502_t *, uint16_t);

static vec2d::Line lines[4096];

static void countedAlphaWrite(m6502_t *c, uint16_t a, uint8_t v) {
  if (a == 0x17c0) {
    gammaWrites++;
  }
  if (a == 0x1640) {
    // decode the list like go() does, without its MAX_LINES cap
    avg::State s = machine->avg_state;
    const avg::Memory m = machine->avg_memory();
    const uint16_t n = avg::decode(m, s, lines, 4096);
    maxLines = n > maxLines ? n : maxLines;
    gos++;
  }
  alphaWrite(c, a, v);
}

static uint8_t countedGammaRead(m6502_t *c, uint16_t a) {
  if ((a & 0xf800) == 0x3000) {
    alphaReads++;
  }
  return gammaRead(c, a);
}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static const uint32_t ATTRACT_FRAMES = 1800, BOOT_SOUND_FRAMES = 240;

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  machine = new mhavoc();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
  alphaRead = machine->m_cpu.read;
  alphaWrite = machine->m_cpu.write;
  machine->m_cpu.write = countedAlphaWrite;
  gammaRead = machine->gamma.read;
  machine->gamma.read = countedGammaRead;
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

// attract mode: AVG lists drawn, every byte alpha sends is read by gamma,
// lists fit dvgMachine::MAX_LINES
static void test_boots_to_attract(void) {
  boot();
  runTo(ATTRACT_FRAMES);
  const uint32_t vram = nonZero(&memory[dvgMachine::VRAM_OFS], 0x1000);
  printf("GOs %u, max lines %u, vram %u, alpha->gamma %u written %u read\n", gos, maxLines, vram,
         gammaWrites, alphaReads);
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_GREATER_THAN_UINT32(ATTRACT_FRAMES / 2, gos);
  TEST_ASSERT_GREATER_THAN_UINT32(500, vram);
  TEST_ASSERT_GREATER_THAN_UINT32(0, gammaWrites);
  TEST_ASSERT_UINT32_WITHIN(1, gammaWrites, alphaReads);
  TEST_ASSERT_LESS_THAN_UINT16(dvgMachine::MAX_LINES, maxLines);
}

// demo sounds on: gamma plays the power-up sounds in the first seconds
// (MAME: AUDC writes in frames 34-200 at 50 Hz) and renderAudio() mixes
// them. Later attract music depends on a RANDOM bit (alpha 0x84a1).
static void test_boot_sound(void) {
  boot();
  uint32_t loud = 0, peak = 0;
  while (frame < BOOT_SOUND_FRAMES) {
    runTo(frame + 1);
    int16_t out[AUDIO_CHUNK];
    machine->renderAudio(out, AUDIO_CHUNK);
    for (int i = 0; i < AUDIO_CHUNK; i++) {
      const uint32_t a = out[i] < 0 ? -out[i] : out[i];
      peak = a > peak ? a : peak;
      loud += a > 16;
    }
  }
  printf("audio: %u loud samples, peak %u\n", loud, peak);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, loud);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(AUDIO_MAX, peak);
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boot_sound);
  RUN_TEST(test_boots_to_attract);
#endif
  return UNITY_END();
}
