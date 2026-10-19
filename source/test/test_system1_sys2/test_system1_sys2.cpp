#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// System 2 board options of the System 1 engine (MAME sega/system1.cpp sys2):
// bank0c main ROM banking, inverted mute bit on PPI port C. Fake PLAIN ROMs
// need no romconv headers.
// Arduino.h / FastLED.h / esp_attr.h here are host stubs.
#define ENABLE_STARJACK  // system1.cpp guard; needs romconv system1_palette.h
#include "../../src/machines/system1/system1_collision.cpp"  // before Z80.h: its `word` macro
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/system1/system1.cpp"
#undef private
#undef protected

static uint8_t mainRom[0x8000];
static uint8_t soundRom[0x2000];
static uint32_t tileGfx[1][8];
static uint16_t tileMap[1];
static uint8_t spriteRom[0x8000];
static uint8_t lookupProm[256];
static uint8_t bankRom[4 * 0x4000];
static uint16_t fakePalette[256];

static constexpr FlashAsset<unsigned char, PLAIN> F_MAIN = {mainRom, sizeof(mainRom)};
static constexpr FlashAsset<unsigned char, PLAIN> F_SOUND = {soundRom, sizeof(soundRom)};
static constexpr FlashAsset<uint32_t[8], PLAIN> F_TILES = {tileGfx, 1};
static constexpr FlashAsset<unsigned short, PLAIN> F_TILE_MAP = {tileMap, 1};
static constexpr FlashAsset<unsigned char, PLAIN> F_SPRITES = {spriteRom, sizeof(spriteRom)};
static constexpr FlashAsset<unsigned char, PLAIN> F_LOOKUP = {lookupProm, sizeof(lookupProm)};
static constexpr FlashAsset<unsigned char, PLAIN> F_BANKS = {bankRom, sizeof(bankRom)};

class fake : public system1 {
public:
  fake(Board board, Bank bank)
      : system1(Roms{F_MAIN, F_SOUND, F_TILES, F_TILE_MAP, F_SPRITES, F_LOOKUP, nullptr, F_BANKS, fakePalette},
                Config{system1_video::VISAREA_FULL, true, board, ROT0,
                       {8, 8, system1_video::NO_PLAYER, 0, 8}, bank}) {}
  signed char machineType() override { return MCH_MENU; }

protected:
  uint8_t dswA() override { return 0xff; }
  uint8_t dswB() override { return 0xff; }
};

static system1 *machine;
static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }
bool Input::demoSoundsOff(void) { return false; }

extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opPrefixZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return machine->inZ80(p); }
void OutZ80(unsigned short p, unsigned char v) { machine->outZ80(p, v); }
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}

static Input input;
static unsigned short frame_buffer[256 * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

static constexpr uint16_t PPI_B_DATA = 0x15;  // video mode, bank bits
static constexpr uint16_t PPI_C_DATA = 0x16;  // sound control

static void boot(system1::Board board, system1::Bank bank) {
  delete machine;
  machine = new fake(board, bank);
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->current_cpu = system1::MAIN_CPU;
}

// peak |sample| of one audio chunk
static int audioPeak() {
  int16_t out[AUDIO_CHUNK];
  machine->renderAudio(out, AUDIO_CHUNK);
  int peak = 0;
  for (int i = 0; i < AUDIO_CHUNK; i++) {
    peak = abs(out[i]) > peak ? abs(out[i]) : peak;
  }
  return peak;
}

// SN76489 #1 tone 0 at full volume
static void playTone() {
  machine->sn[0].write(0x80 | 0x0a);  // latch tone 0, period low
  machine->sn[0].write(0x01);         // period high
  machine->sn[0].write(0x90);         // latch volume 0, attenuation 0
}

void setUp(void) {
  for (uint32_t i = 0; i < sizeof(bankRom); i++) {
    bankRom[i] = (uint8_t)(0xb0 + i / 0x4000);  // byte = bank number
  }
  for (int i = 0; i < 256; i++) {
    fakePalette[i] = (uint16_t)(0x1000 + i);
  }
  memset(mainRom, 0x5a, sizeof(mainRom));
  keys = 0;
}
void tearDown(void) {}

// MAME bank0c_custom_w: entry = (mode & 0x0c) >> 2, other mode bits ignored
static void test_bank0c_select(void) {
  boot(system1::BOARD_SYS2, system1::BANK_0C);
  TEST_ASSERT_EQUAL_UINT8(0xb0, machine->rdZ80(0x8000));
  const uint8_t modes[4] = {0x00, 0x04, 0x08, 0x0c};
  for (int e = 0; e < 4; e++) {
    machine->outZ80(PPI_B_DATA, modes[e] | 0xf3);
    TEST_ASSERT_EQUAL_UINT8(0xb0 + e, machine->rdZ80(0x8000));
    TEST_ASSERT_EQUAL_UINT8(0xb0 + e, machine->opZ80(0xbfff));
  }
  machine->outZ80(PPI_B_DATA, 0x08);
  TEST_ASSERT_EQUAL_UINT8(0xb2, machine->rdZ80(0x8000));
  machine->reset();
  TEST_ASSERT_EQUAL_UINT8(0xb0, machine->rdZ80(0x8000));
}

// MAME sound_control_w: mute = (data ^ mute_xor) & 1, mute_xor 1 on System 2
static void test_sys2_mute_inverted(void) {
  boot(system1::BOARD_SYS2, system1::BANK_0C);
  playTone();
  machine->outZ80(PPI_C_DATA, 0x01);
  TEST_ASSERT_GREATER_THAN(0, audioPeak());
  machine->outZ80(PPI_C_DATA, 0x00);
  TEST_ASSERT_EQUAL_INT(0, audioPeak());
  machine->outZ80(PPI_C_DATA, 0x81);
  TEST_ASSERT_GREATER_THAN(0, audioPeak());
}

// System 1 PPI boards keep sound on whatever port C bit 0 says (unchanged)
static void test_sys1_ppi_never_muted(void) {
  boot(system1::BOARD_PPI, system1::BANK_NONE);
  playTone();
  machine->outZ80(PPI_C_DATA, 0x00);
  TEST_ASSERT_GREATER_THAN(0, audioPeak());
  machine->outZ80(PPI_C_DATA, 0x01);
  TEST_ASSERT_GREATER_THAN(0, audioPeak());
}

int main(int, char **) {
  Arena::init();
  UNITY_BEGIN();
  RUN_TEST(test_bank0c_select);
  RUN_TEST(test_sys2_mute_inverted);
  RUN_TEST(test_sys1_ppi_never_muted);
  return UNITY_END();
}
