#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Boots the Juno First ROM on the host through the machine class: KONAMI-1
// main CPU with blitter, sound Z80 (AY), i8039 DAC. Writes one frame to
// $TMPDIR/junofrst_frame.ppm for a look by eye. ROM headers come from
// romconv (./convert.sh junofrst); without them the test is skipped.
#if __has_include("../../src/machines/junofrst/junofrst_rom.h")
#define HAVE_ROM 1
#define ENABLE_JUNOFRST
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/cpus/i8048/i8048.c"
#include "../../src/emulation/ay8910.cpp"
#define private public
#define protected public
#include "../../src/machines/junofrst/junofrst.cpp"
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
static junofrst *machine;
static uint32_t frame;
static uint32_t latchWrites, i8039Ints, latchReads, sndEdges;
static uint8_t sndLast;

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) {
  latchReads += a == 0x3000;
  return machine->rdZ80(a);
}
void WrZ80(unsigned short a, unsigned char v) {
  latchWrites += a == 0x5000;
  i8039Ints += a == 0x6000;
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
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) {
  if (a == 0x8040) {
    sndEdges += !sndLast && v == 1;
    sndLast = v;
  }
  machine->m6809_write(s, a, v);
}

static uint32_t dacChanges;   // DAC ring samples unlike the one before
static uint8_t dacPrev;

static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new junofrst();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// credit counter in work RAM (MAME junofrst: $8823 0 -> 1 on a coin, 0 after start)
static const uint16_t CREDITS = 0x8823 - 0x8100;
static const uint32_t COIN_FRAME = 1000, START_FRAME = 1200;

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

// ---- blitter, on a scratch machine with a test blitter ROM ----

static uint8_t blitRom[junofrst::BLIT_SIZE];
static uint8_t vram[junofrst::VRAM_SIZE];

static uint8_t *liveVram;

static void blitWith(uint16_t dest, uint16_t src) {
  boot();
  liveVram = machine->videoram;
  machine->blit_rom = blitRom;
  machine->videoram = vram;
  machine->m6809_write(&machine->main_cpu, 0x8070, dest >> 8);
  machine->m6809_write(&machine->main_cpu, 0x8071, dest & 0xff);
  machine->m6809_write(&machine->main_cpu, 0x8072, src >> 8);
  machine->m6809_write(&machine->main_cpu, 0x8073, src & 0xff);
}

static void restore(void) {
  machine->blit_rom = machine->junofrst_blit.data();
  machine->videoram = liveVram;
}

// nibble 0 = high nibble of byte 0; row stride 256 nibbles; zero source
// nibbles leave the destination alone
static void setupPattern(void) {
  memset(blitRom, 0, sizeof(blitRom));
  blitRom[0] = 0x12;   // row 0: nibbles 1, 2
  blitRom[8] = 0x30;   // row 1: nibble 3
  memset(vram, 0xff, sizeof(vram));
}

static void test_blit_copy(void) {
  setupPattern();
  blitWith(0x0100, 0x0001);   // src bit 0: copy
  TEST_ASSERT_EQUAL_HEX8(0x21, vram[0x80]);
  TEST_ASSERT_EQUAL_HEX8(0xff, vram[0x81]);
  TEST_ASSERT_EQUAL_HEX8(0xf3, vram[0x100]);
  restore();
}

static void test_blit_clear(void) {
  setupPattern();
  blitWith(0x0100, 0x0000);   // src bit 0 clear: erase where source is set
  TEST_ASSERT_EQUAL_HEX8(0x00, vram[0x80]);
  TEST_ASSERT_EQUAL_HEX8(0xff, vram[0x81]);
  TEST_ASSERT_EQUAL_HEX8(0xf0, vram[0x100]);
  restore();
}

// dest near the end of the 64 K nibbles wraps into video RAM
static void test_blit_wraps(void) {
  memset(blitRom, 0x11, sizeof(blitRom));
  memset(vram, 0, sizeof(vram));
  blitWith(0xfff8, 0x0001);
  TEST_ASSERT_EQUAL_HEX8(0x11, vram[0x7ffc]);
  TEST_ASSERT_EQUAL_HEX8(0x11, vram[0x0000]);
  TEST_ASSERT_EQUAL_HEX8(0x11, vram[0x0003]);
  TEST_ASSERT_EQUAL_HEX8(0x00, vram[0x0004]);
  restore();
}

// source past the 24 KB blitter ROM reads as blank
static void test_blit_src_past_rom(void) {
  memset(blitRom, 0x11, sizeof(blitRom));
  memset(vram, 0, sizeof(vram));
  blitWith(0x0000, 0xfffd);
  for (uint32_t i = 0; i < sizeof(vram); i++) {
    TEST_ASSERT_EQUAL_HEX8(0, vram[i]);
  }
  restore();
}

// ---- banked ROM ----

static void test_bank_opcode_decrypt(void) {
  boot();
  machine->m6809_write(&machine->main_cpu, 0x8060, 2);
  const uint8_t *b = machine->bank_rom + 2 * 0x1000;
  TEST_ASSERT_EQUAL_HEX8(b[0x10], machine->m6809_read(&machine->main_cpu, 0x9010));
  TEST_ASSERT_EQUAL_HEX8(b[0x10] ^ 0x22, machine->m6809_read_opcode(&machine->main_cpu, 0x9010));
  TEST_ASSERT_EQUAL_HEX8(b[0x12] ^ 0x82, machine->m6809_read_opcode(&machine->main_cpu, 0x9012));
  TEST_ASSERT_EQUAL_HEX8(b[0x18] ^ 0x28, machine->m6809_read_opcode(&machine->main_cpu, 0x9018));
  TEST_ASSERT_EQUAL_HEX8(b[0x1a] ^ 0x88, machine->m6809_read_opcode(&machine->main_cpu, 0x901a));
}

static void test_bank_out_of_range(void) {
  boot();
  machine->m6809_write(&machine->main_cpu, 0x8060, 12);
  TEST_ASSERT_EQUAL_HEX8(0xff, machine->m6809_read(&machine->main_cpu, 0x9000));
  machine->m6809_write(&machine->main_cpu, 0x8060, 0x1b);   // only b0-3 count
  TEST_ASSERT_EQUAL_HEX8(machine->bank_rom[0xb000], machine->m6809_read(&machine->main_cpu, 0x9000));
}

// ---- boot ----

static uint32_t differing(const uint8_t *p, uint32_t n) {
  uint32_t c = 0;
  for (uint32_t i = 0; i < n; i++) {
    c += p[i] != p[0];
  }
  return c;
}

// attract: the title is drawn into the bitmap, the sound CPU gets commands
static void test_boots_to_attract(void) {
  boot();
  runTo(600);
  const uint32_t drawn = differing(machine->videoram, junofrst::VRAM_SIZE);
  printf("bitmap bytes %u, latch2 writes %u, i8039 INTs %u\n", drawn, latchWrites, i8039Ints);
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, drawn);
}

static void test_coin_and_start(void) {
  boot();
  runTo(COIN_FRAME - 10);
  TEST_ASSERT_EQUAL_UINT8(0, memory[JUNOFRST_WORKRAM + CREDITS]);
  runTo(COIN_FRAME + 100);
  TEST_ASSERT_EQUAL_UINT8(1, memory[JUNOFRST_WORKRAM + CREDITS]);
  runTo(START_FRAME + 500);
  TEST_ASSERT_EQUAL_UINT8(0, memory[JUNOFRST_WORKRAM + CREDITS]);
}

// in play: sound commands reach the Z80, the i8039 moves the DAC
static void test_sound_runs(void) {
  boot();
  runTo(START_FRAME + 1500);
  printf("IRQ edges %u, latch reads %u, latch2 writes %u, i8039 INTs %u, DAC changes %u\n",
         sndEdges, latchReads, latchWrites, i8039Ints, dacChanges);
  TEST_ASSERT_GREATER_THAN_UINT32(0, sndEdges);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(sndEdges, latchReads);
  TEST_ASSERT_GREATER_THAN_UINT32(0, latchWrites);
  TEST_ASSERT_GREATER_THAN_UINT32(0, i8039Ints);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, dacChanges);
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
  snprintf(path, sizeof(path), "%s/junofrst_frame.ppm", dir ? dir : "/tmp");
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
  RUN_TEST(test_blit_copy);
  RUN_TEST(test_blit_clear);
  RUN_TEST(test_blit_wraps);
  RUN_TEST(test_blit_src_past_rom);
  RUN_TEST(test_bank_opcode_decrypt);
  RUN_TEST(test_bank_out_of_range);
  RUN_TEST(test_boots_to_attract);
  RUN_TEST(test_coin_and_start);
  RUN_TEST(test_sound_runs);
  RUN_TEST(test_render_frame);
#endif
  return UNITY_END();
}
