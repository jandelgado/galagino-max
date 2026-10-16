// MCR Super Sound I/O board: memory map, 14024 IRQ counter (MAME
// midway.cpp clock_14024/irq_clear), AY duty-cycle volume and mute.
#include <string.h>
#include <unity.h>

#include "../../src/cpus/z80/Z80.c"
#define TRON_Z80_TABLES_INCLUDED
#include "../../src/emulation/ay8910.cpp"
#include "../../src/machines/tron/tron_ssio.cpp"

static uint8_t rom[0x3000];
static uint8_t ram[1024];
static Z80 cpu;
static TronSsio ssio;

// duty nibble -> gain/16: full at 0, silent at 15
static const uint8_t DUTY[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 0};

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return ssio.rd(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return ssio.rd(a); }
unsigned char RdZ80(unsigned short a) { return ssio.rd(a); }
void WrZ80(unsigned short a, unsigned char v) { ssio.wr(a, v); }
unsigned char InZ80(unsigned short) { return 0xff; }
void OutZ80(unsigned short, unsigned char) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}

// IM 1; LD SP,8400h; EI; JR $   ISR at 38h: INC (8000h) [; LD A,(E000h)]; EI; RET
static void loadProgram(bool clearIrq) {
  memset(rom, 0, sizeof(rom));
  const uint8_t main[] = {0xed, 0x56, 0x31, 0x00, 0x84, 0xfb, 0x18, 0xfe};
  memcpy(rom, main, sizeof(main));
  const uint8_t isrClear[] = {0x21, 0x00, 0x80, 0x34, 0x3a, 0x00, 0xe0, 0xfb, 0xc9};
  const uint8_t isrKeep[] = {0x21, 0x00, 0x80, 0x34, 0xfb, 0xc9};
  if (clearIrq) {
    memcpy(rom + 0x38, isrClear, sizeof(isrClear));
  } else {
    memcpy(rom + 0x38, isrKeep, sizeof(isrKeep));
  }
}

void setUp(void) {
  memset(ram, 0, sizeof(ram));
  ssio.attach(rom, ram, DUTY);
  ssio.reset();
  ResetZ80(&cpu);
}

void tearDown(void) {}

static void test_irq_rate_with_clear(void) {
  loadProgram(true);
  ResetZ80(&cpu);
  ssio.run(cpu, 33333);  // one 60 Hz frame at 2 MHz
  // first IRQ after 64 x 40 cycles, then 64 x 40 after each clear: ~780 Hz
  TEST_ASSERT_INT_WITHIN(1, 13, ram[0]);
}

static void test_irq_line_held_without_clear(void) {
  loadProgram(false);
  ResetZ80(&cpu);
  ssio.run(cpu, 64 * 40 - 100);
  TEST_ASSERT_EQUAL(0, ram[0]);
  ssio.run(cpu, 64 * 40 + 200);  // line high for 64 counts: ISR re-entered
  const uint8_t inWindow = ram[0];
  TEST_ASSERT_GREATER_THAN(10, inWindow);
  ssio.run(cpu, 64 * 40 - 200);  // count wrapped to 0: line low
  TEST_ASSERT_EQUAL(inWindow, ram[0]);
}

static void test_latches_and_status(void) {
  ssio.latch(1, 0x55);
  TEST_ASSERT_EQUAL_HEX8(0x55, ssio.rd(0x9001));
  TEST_ASSERT_EQUAL_HEX8(0x55, ssio.rd(0x9ffd));  // mirror 0x0ffc
  ssio.wr(0xc000, 0x12);
  TEST_ASSERT_EQUAL_HEX8(0x12, ssio.status());
  ssio.wr(0x8c05, 0x77);                           // RAM mirror 0x0c00
  TEST_ASSERT_EQUAL_HEX8(0x77, ssio.rd(0x8005));
}

static void ayWrite(uint16_t base, uint8_t reg, uint8_t v) {
  ssio.wr(base, reg);
  ssio.wr(base + 2, v);
}

static int peak(void) {
  int16_t out[AUDIO_CHUNK];
  ssio.render(out, AUDIO_CHUNK);
  int p = 0;
  for (int i = 0; i < AUDIO_CHUNK; i++) {
    p = out[i] > p ? out[i] : (-out[i] > p ? -out[i] : p);
  }
  return p;
}

static void toneOnAy0(void) {
  ayWrite(0xa000, 0, 0x40);
  ayWrite(0xa000, 7, 0x3e);
  ayWrite(0xa000, 8, 0x0f);
}

static void test_ay_tone_audible(void) {
  toneOnAy0();
  TEST_ASSERT_GREATER_THAN(0, peak());
  TEST_ASSERT_LESS_OR_EQUAL(AUDIO_MAX, peak());
}

static void test_duty_cycle_silences_voice(void) {
  toneOnAy0();
  ayWrite(0xa000, 14, 0x0f);  // port A low nibble: voice 0 duty 15
  TEST_ASSERT_EQUAL(0, peak());
}

static void test_mute_bit(void) {
  toneOnAy0();
  ayWrite(0xb000, 15, 0x80);  // AY1 port B bit 7 mutes both chips
  TEST_ASSERT_EQUAL(0, peak());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_irq_rate_with_clear);
  RUN_TEST(test_irq_line_held_without_clear);
  RUN_TEST(test_latches_and_status);
  RUN_TEST(test_ay_tone_audible);
  RUN_TEST(test_duty_cycle_silences_voice);
  RUN_TEST(test_mute_bit);
  return UNITY_END();
}
