#include <stdio.h>
#include <string.h>
#include <unity.h>

// Boots the Jail Break ROM (Konami-1 6809) on the host through the jailbrek
// machine class. ROM headers come from romconv (./convert.sh jailbrek);
// without them the test is skipped.
#if __has_include("../../src/machines/jailbrek/jailbrek_rom.h")
#define HAVE_ROM 1
#define GAME_JAILBREK
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#include "../../src/emulation/vlm5030.cpp"
#define private public
#define protected public
#include "../../src/machines/jailbrek/jailbrek.cpp"
#undef private
#undef protected
#include "mame_jailbrek_vram.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }

static jailbrek *machine;
static uint32_t nmis, irqs;  // vector fetches, as the MAME read tap counts them

unsigned char m6809_read(m6809_state *s, uint16_t a) {
  nmis += a == 0xfffc;
  irqs += a == 0xfff8;
  return machine->m6809_read(s, a);
}
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) {
  return machine->m6809_read_opcode(s, a);
}
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }
extern "C" {
unsigned char OpZ80_INL(unsigned short) { return 0; }
unsigned char OpPrefixZ80_INL(unsigned short) { return 0; }
unsigned char RdZ80(unsigned short) { return 0; }
void WrZ80(unsigned short, unsigned char) {}
unsigned char InZ80(unsigned short) { return 0; }
void OutZ80(unsigned short, unsigned char) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static uint32_t frame;
static uint32_t badPcFrames;  // PC outside ROM (8000-ffff) and work RAM (1200-1fff)

static void boot(void) {
  static bool booted = false;
  if (booted) {
    return;
  }
  booted = true;
  Arena::init();
  Arena::reserve(&jailbrek_arena_plan);  // as main.cpp's switchTo()
  machine = new jailbrek();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void runTo(uint32_t f) {
  while (frame < f) {
    machine->run_frame();
    frame++;
    const uint16_t pc = machine->main_cpu.PC;
    badPcFrames += !(pc >= 0x8000 || (pc >= 0x1200 && pc < 0x2000));
  }
}

// MAME (vector read tap, frames 250-3600): 8 NMIs every frame once ctrl4 b0
// is set, the handler clears and sets b0 to re-arm the held line.
static void test_nmi_8_per_frame(void) {
  boot();
  runTo(400);
  TEST_ASSERT_TRUE(machine->ctrl[4] & 0x01);
  uint32_t irq_frames = 0;
  for (int i = 0; i < 60; i++) {
    nmis = irqs = 0;
    runTo(frame + 1);
    TEST_ASSERT_EQUAL_UINT32(8, nmis);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1, irqs);
    irq_frames += irqs;
  }
  printf("frames with vblank IRQ: %u of 60\n", irq_frames);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(55, irq_frames);
}

// score ranking, static (MAME frames 550-650 equal)
static void test_ranking_vram_matches_mame(void) {
  boot();
  runTo(600);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(mame_jailbrek_vram_600, machine->memory + JB_MEM_CRAM, 0x1000);
}

static uint32_t differing(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += a[i] != b[i];
  }
  return c;
}

// gameplay demo in the jail: background static, a few HUD bytes tick
static void test_demo_vram_matches_mame(void) {
  boot();
  runTo(900);
  const uint32_t d = differing(mame_jailbrek_vram_900, machine->memory + JB_MEM_CRAM, 0x1000);
  printf("demo VRAM bytes unlike MAME: %u\n", d);
  TEST_ASSERT_EQUAL_UINT32(0, d);
}

// one minute of attract: title, ranking, demo, no runaway CPU
static void test_attract_runs(void) {
  boot();
  runTo(3600);
  printf("frames with PC outside ROM/RAM: %u\n", badPcFrames);
  TEST_ASSERT_EQUAL_UINT32(0, badPcFrames);
  TEST_ASSERT_EQUAL_UINT8(1, machine->game_started);
}

static void test_dsw(void) {
  boot();
  TEST_ASSERT_EQUAL_HEX8(0x59, machine->m6809_read(&machine->main_cpu, 0x3100));
  TEST_ASSERT_EQUAL_HEX8(0x0f, machine->m6809_read(&machine->main_cpu, 0x3200));
  TEST_ASSERT_EQUAL_HEX8(0xff, machine->m6809_read(&machine->main_cpu, 0x3303));
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_nmi_8_per_frame);
  RUN_TEST(test_ranking_vram_matches_mame);
  RUN_TEST(test_demo_vram_matches_mame);
  RUN_TEST(test_attract_runs);
  RUN_TEST(test_dsw);
#else
  printf("jailbrek ROM headers missing, run romconv\n");
#endif
  return UNITY_END();
}
