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

// opcode fetches at the idle loop entries: gamma main loop, alpha IRQ
// wait, alpha delay loop, alpha wait for the gamma reply
static const uint16_t IDLE_PC[4] = {0x802c, 0x821c, 0x91ca, 0x917a};
static uint32_t idleFetches[4], instructions[2]; // instructions: alpha, gamma
static uint8_t fetchGamma(m6502_t *c, uint16_t a) {
  idleFetches[0] += a == IDLE_PC[0];
  instructions[1]++;
  return gammaRead(c, a);
}
static uint8_t fetchAlpha(m6502_t *c, uint16_t a) {
  idleFetches[1] += a == IDLE_PC[1];
  instructions[0]++;
  idleFetches[2] += a == IDLE_PC[2];
  idleFetches[3] += a == IDLE_PC[3];
  return alphaRead(c, a);
}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];

static const uint32_t ATTRACT_FRAMES = 1800, BOOT_SOUND_FRAMES = 240, BLANK_FROM = 600;

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
  machine->m_cpu.fetch = fetchAlpha;
  machine->gamma.fetch = fetchGamma;
}

static uint32_t blankFrames; // after BLANK_FROM: frames whose published list is empty

static void runTo(uint32_t f) {
  while (frame < f) {
    machine->run_frame();
    frame++;
    uint16_t count = 0;
    machine->go_list->visit([&count](const dvgMachine::GoList &g) { count = g.count; });
    blankFrames += frame >= BLANK_FROM && !count;
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
// lists fit dvgMachine::MAX_LINES. The IRQ handler's beam refresh GOs
// (JSRL to ROM, no lines) between two game lists keep the screen.
static void test_boots_to_attract(void) {
  boot();
  runTo(ATTRACT_FRAMES);
  const uint32_t vram = nonZero(&memory[dvgMachine::VRAM_OFS], 0x1000);
  printf("GOs %u, max lines %u, vram %u, alpha->gamma %u written %u read, blank frames %u\n", gos,
         maxLines, vram, gammaWrites, alphaReads, blankFrames);
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_GREATER_THAN_UINT32(ATTRACT_FRAMES / 2, gos);
  TEST_ASSERT_GREATER_THAN_UINT32(500, vram);
  TEST_ASSERT_GREATER_THAN_UINT32(0, gammaWrites);
  TEST_ASSERT_UINT32_WITHIN(1, gammaWrites, alphaReads);
  TEST_ASSERT_LESS_THAN_UINT16(dvgMachine::MAX_LINES, maxLines);
  TEST_ASSERT_EQUAL_UINT32(0, blankFrames);
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

static void hold(unsigned int k, uint32_t frames) {
  keys = k;
  runTo(frame + frames);
  keys = 0;
}

// alpha zero page: credits
static const uint16_t CREDITS = 0x00ac;

// COIN gives one credit; START (sent by input.cpp ~36 frames after its
// virtual coin) only presses FIRE, which starts the game and spends it.
// Then a minute of scripted play: lists stay below MAX_LINES, no blank
// frames.
static void test_coin_start_and_play(void) {
  boot();
  runTo(ATTRACT_FRAMES + 600);
  hold(BUTTON_COIN, 6);
  runTo(frame + 60); // the game credits a coin 30-40 frames after the pulse
  TEST_ASSERT_EQUAL_UINT8(1, memory[CREDITS]);
  hold(BUTTON_START, 6);
  runTo(frame + 120);
  TEST_ASSERT_EQUAL_UINT8(0, memory[CREDITS]);

  maxLines = 0;
  blankFrames = 0;
  for (int i = 0; i < 60; i++) {
    hold(BUTTON_FIRE | (i & 4 ? BUTTON_LEFT : BUTTON_RIGHT), 10);
    hold((i & 1) ? BUTTON_DOWN : 0, 50);
  }
  printf("play: max lines %u, blank frames %u, credits %u\n", maxLines, blankFrames, memory[CREDITS]);
  TEST_ASSERT_LESS_THAN_UINT16(dvgMachine::MAX_LINES, maxLines);
  TEST_ASSERT_EQUAL_UINT32(0, blankFrames);
}

// holding the stick turns the roller about as fast as MAME's key delta
// (40 counts per frame): over 600 counts in half a second
static void test_roller_speed(void) {
  boot();
  keys = BUTTON_RIGHT;
  const uint16_t from = machine->roller.pos;
  for (int f = 0; f < 30; f++) {
    machine->control_input();
  }
  keys = 0;
  machine->control_input();
  const uint16_t counts = uint16_t(from - machine->roller.pos) / 4; // right counts down (PORT_REVERSE in read)
  const uint16_t up = uint16_t(machine->roller.pos - from) / 4;
  printf("roller: %u counts in 30 frames\n", counts < up ? counts : up);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT16(600, counts < up ? counts : up);
}

// idle loops are skipped, not stepped: a few fetches per frame (gamma
// re-enters its loop after each of its ~10 interrupts per frame, alpha
// steps the pass cut by each slice end) instead
// of 214 / 108 / 324 / 128
static void test_idle_loops_skipped(void) {
  boot();
  runTo(ATTRACT_FRAMES);
  memset(idleFetches, 0, sizeof(idleFetches));
  memset(instructions, 0, sizeof(instructions));
  const uint32_t frames = 600;
  runTo(ATTRACT_FRAMES + frames);
  printf("idle fetches per frame: gamma %u, alpha wait %u, alpha delay %u, alpha reply wait %u\n",
         idleFetches[0] / frames, idleFetches[1] / frames, idleFetches[2] / frames, idleFetches[3] / frames);
  printf("instructions per frame: alpha %u, gamma %u\n", instructions[0] / frames, instructions[1] / frames);
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_LESS_THAN_UINT32(40 * frames, idleFetches[i]);
  }
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boot_sound);
  RUN_TEST(test_boots_to_attract);
  RUN_TEST(test_idle_loops_skipped);
  RUN_TEST(test_coin_start_and_play);
  RUN_TEST(test_roller_speed);
#endif
  return UNITY_END();
}
