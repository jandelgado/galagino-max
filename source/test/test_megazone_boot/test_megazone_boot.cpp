#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots the Mega Zone ROM on the host through the machine class: KONAMI-1
// main CPU, sound Z80 (inputs, AY), i8039 DAC. Writes one frame to
// $TMPDIR/megazone_frame.ppm for a look by eye. ROM headers come from
// romconv (./convert.sh megazone); without them the test is skipped.
#if __has_include("../../src/machines/megazone/megazone_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_MEGAZONE
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/cpus/i8048/i8048.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/megazone/megazone.cpp"
#include "../../src/emulation/i8039_dac.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

static Input input;
static unsigned short frame_buffer[224 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static megazone *machine;
static uint32_t frame;
static uint32_t latchWrites, i8039Ints;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) {
  latchWrites += a == 0x4000;
  i8039Ints += a == 0x2000;
  machine->wrZ80(a, v);
}
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
void i8048_port_write(i8048_state_S *s, unsigned char p, unsigned char v) { machine->wrI8048_port(s, p, v); }
unsigned char i8048_port_read(i8048_state_S *s, unsigned char p) { return machine->rdI8048_port(s, p); }
unsigned char i8048_rom_read(i8048_state_S *s, unsigned short a) { return machine->rdI8048_rom(s, a); }
unsigned char i8048_xdm_read(i8048_state_S *s, unsigned char a) { return machine->rdI8048_xdm(s, a); }
void i8048_xdm_write(i8048_state_S *, unsigned char, unsigned char) {}
}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }

// credit counter in shared RAM (MAME megazone: $3b1d 0 -> 1 on a coin, 0 after start)
static const uint16_t CREDITS = 0x3b1d - 0x2000;
static const uint32_t COIN_FRAME = 1000, START_FRAME = 1200;
static uint32_t dacChanges;   // DAC ring samples unlike the one before
static uint8_t dacPrev;

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new megazone();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

static void runTo(uint32_t end) {
  while (frame < end) {
    keys = 0;
    if (frame >= COIN_FRAME && frame < COIN_FRAME + 5) {
      keys = BUTTON_COIN;
    }
    if (frame >= START_FRAME && frame < START_FRAME + 5) {
      keys = BUTTON_START;
    }
    if (frame > START_FRAME + 100 && frame % 20 < 4) {
      keys = BUTTON_FIRE;
    }
    machine->run_frame();
    frame++;
    I8039Dac &dac = machine->dac;
    for (uint16_t i = dac.rd; i != dac.wr; i = (i + 1) & (I8039Dac::RING - 1)) {
      dacChanges += dac.ring[i] != dacPrev;
      dacPrev = dac.ring[i];
    }
    dac.rd = dac.wr;   // drain as renderAudio() would
  }
  keys = 0;
}

static uint32_t differing(const uint8_t *p, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += p[i] != p[0];
  }
  return c;
}

// attract: title on the playfield, scores on the panel, sound commands
static void test_boots_to_attract(void) {
  boot();
  runTo(600);
  const uint32_t field = differing(&memory[MEGAZONE_VRAM0], 0x400);
  const uint32_t panel = differing(&memory[MEGAZONE_VRAM1], 0x400);
  printf("playfield %u, panel %u, latch writes %u, i8039 INTs %u\n", field, panel, latchWrites,
         i8039Ints);
  TEST_ASSERT_GREATER_THAN_UINT32(50, field);
  TEST_ASSERT_GREATER_THAN_UINT32(20, panel);
}

// coins and start go through the sound CPU and the shared RAM
static void test_coin_and_start(void) {
  boot();
  runTo(COIN_FRAME - 10);
  TEST_ASSERT_EQUAL_UINT8(0, memory[CREDITS]);
  runTo(COIN_FRAME + 100);
  TEST_ASSERT_EQUAL_UINT8(1, memory[CREDITS]);
  runTo(START_FRAME + 500);
  TEST_ASSERT_EQUAL_UINT8(0, memory[CREDITS]);
}

// in play the sound CPU sends drum commands: the i8039 moves the DAC
static void test_drums_on_dac(void) {
  boot();
  runTo(START_FRAME + 1500);
  printf("latch writes %u, i8039 INTs %u, DAC changes %u\n", latchWrites, i8039Ints, dacChanges);
  TEST_ASSERT_GREATER_THAN_UINT32(0, i8039Ints);
  TEST_ASSERT_GREATER_THAN_UINT32(2400, dacChanges);
}

static void putPixel(FILE *f, uint16_t v) {
  v = (uint16_t)((v << 8) | (v >> 8));
  const uint8_t rgb[3] = {(uint8_t)((v >> 8) & 0xf8), (uint8_t)((v >> 3) & 0xfc),
                          (uint8_t)((v << 3) & 0xf8)};
  fwrite(rgb, 1, 3, f);
}

static void test_render_frame(void) {
  boot();
  machine->prepare_frame();
  const char *dir = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof(path), "%s/megazone_frame.ppm", dir ? dir : "/tmp");
  FILE *f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL(f);
  fprintf(f, "P6\n224 288\n255\n");
  uint32_t lit = 0;
  for (int row = 0; row < 36; row++) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    machine->render_row(row);
    for (int i = 0; i < 224 * 8; i++) {
      putPixel(f, frame_buffer[i]);
      lit += frame_buffer[i] != 0;
    }
  }
  fclose(f);
  printf("frame %u written to %s, %u non-black px\n", frame, path, lit);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, lit);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_boots_to_attract);
  RUN_TEST(test_coin_and_start);
  RUN_TEST(test_drums_on_dac);
  RUN_TEST(test_render_frame);
#endif
  return UNITY_END();
}
