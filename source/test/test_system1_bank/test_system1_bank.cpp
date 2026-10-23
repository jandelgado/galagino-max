#include <stdint.h>
#include <string.h>
#include <unity.h>

// System 1 engine options for Heavy Metal on fake PLAIN ROMs (no romconv
// headers needed): PLAIN assets read in place, bank44 main ROM banking at
// 8000-bfff selected by video mode bits 6 and 2, palette from a per-game
// table, FIRE on P1 bit 1 for sets whose MAME BUTTON1 sits there.
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
  explicit fake(Bank bank, Buttons buttons = BUTTONS_GENERIC)
      : system1(Roms{F_MAIN, F_SOUND, F_TILES, F_TILE_MAP, F_SPRITES, F_LOOKUP, nullptr, F_BANKS, fakePalette},
                Config{system1_video::VISAREA_FULL, false, BOARD_PIO, ROT0,
                       {8, 8, system1_video::NO_PLAYER, 0, 0}, bank, buttons}) {}
  signed char machineType() override { return MCH_MENU; }

protected:
  uint8_t dswA() override { return 0xff; }
  uint8_t dswB() override { return 0xff; }
};

static fake *machine;
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

static Input input;
static unsigned short frame_buffer[256 * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

static constexpr uint16_t PIO_B_DATA = 0x19;

static void boot(system1::Bank bank, system1::Buttons buttons = system1::BUTTONS_GENERIC) {
  delete machine;
  machine = new fake(bank, buttons);
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->current_cpu = system1::MAIN_CPU;
}

void setUp(void) {
  for (uint32_t i = 0; i < sizeof(bankRom); i++) {
    bankRom[i] = (uint8_t)(0xb0 + i / 0x4000);  // byte = bank number
  }
  for (int i = 0; i < 256; i++) {
    fakePalette[i] = (uint16_t)(0x1000 + i);
  }
  memset(mainRom, 0x5a, sizeof(mainRom));
}
void tearDown(void) {}

static void test_plain_assets_read_in_place(void) {
  boot(system1::BANK_44);
  TEST_ASSERT_EQUAL_PTR(mainRom, machine->mainRom);
  TEST_ASSERT_EQUAL_PTR(spriteRom, machine->spriteRom);
  TEST_ASSERT_EQUAL_PTR(tileGfx, machine->tiles.gfx);
  TEST_ASSERT_EQUAL_UINT8(0x5a, machine->rdZ80(0x1234));
  TEST_ASSERT_EQUAL_UINT8(0x5a, machine->opZ80(0x1234));
}

static void test_bank_0_after_reset(void) {
  boot(system1::BANK_44);
  TEST_ASSERT_EQUAL_UINT8(0xb0, machine->rdZ80(0x8000));
  TEST_ASSERT_EQUAL_UINT8(0xb0, machine->opZ80(0xbfff));
}

// MAME bank44_custom_w: entry = (mode & 0x40) >> 5 | (mode & 0x04) >> 2
static void test_bank44_select(void) {
  boot(system1::BANK_44);
  const uint8_t modes[4] = {0x00, 0x04, 0x40, 0x44};
  for (int e = 0; e < 4; e++) {
    machine->outZ80(PIO_B_DATA, modes[e] | 0x11);  // other mode bits ignored
    TEST_ASSERT_EQUAL_UINT8(0xb0 + e, machine->rdZ80(0x8000));
    TEST_ASSERT_EQUAL_UINT8(0xb0 + e, machine->opZ80(0xa000));
  }
  machine->reset();
  TEST_ASSERT_EQUAL_UINT8(0xb0, machine->rdZ80(0x8000));
}

static void test_no_bank_reads_open_bus(void) {
  boot(system1::BANK_NONE);
  machine->outZ80(PIO_B_DATA, 0x44);
  TEST_ASSERT_EQUAL_UINT8(0xff, machine->rdZ80(0x8000));  // main ROM is 0x8000 long
}

static void test_palette_from_game_table(void) {
  boot(system1::BANK_44);
  machine->wrZ80(0xd800 + 3, 0x7e);
  TEST_ASSERT_EQUAL_UINT16(0x1000 + 0x7e, machine->palette[3]);
}

static constexpr uint16_t PORT_P1 = 0x00;

// generic: FIRE = bit 2 (MAME BUTTON1); swapped: FIRE = bit 1 (Heavy Metal)
static void test_fire_bit(void) {
  keys = BUTTON_FIRE;
  boot(system1::BANK_NONE);
  TEST_ASSERT_EQUAL_HEX8(0xfb, machine->inZ80(PORT_P1));
  boot(system1::BANK_NONE, system1::BUTTONS_SWAPPED);
  TEST_ASSERT_EQUAL_HEX8(0xfd, machine->inZ80(PORT_P1));
  keys = BUTTON_B;
  TEST_ASSERT_EQUAL_HEX8(0xfb, machine->inZ80(PORT_P1));
  keys = 0;
}

int main(int, char **) {
  Arena::init();
  UNITY_BEGIN();
  RUN_TEST(test_plain_assets_read_in_place);
  RUN_TEST(test_bank_0_after_reset);
  RUN_TEST(test_bank44_select);
  RUN_TEST(test_no_bank_reads_open_bus);
  RUN_TEST(test_palette_from_game_table);
  RUN_TEST(test_fire_bit);
  return UNITY_END();
}
