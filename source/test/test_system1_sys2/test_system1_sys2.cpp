#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// System 2 board options of the System 1 engine (MAME sega/system1.cpp sys2):
// bank0c main ROM banking, inverted mute bit on PPI port C. Fake PLAIN ROMs
// need no romconv headers; Choplifter boots from its real ROMs if
// ./convert.sh choplift ran, else its tests are skipped.
// Arduino.h / FastLED.h / esp_attr.h here are host stubs.
#define ENABLE_STARJACK  // system1.cpp guard; needs romconv system1_palette.h
#if __has_include("../../src/machines/choplift/choplift_main_rom.h")
#define ENABLE_CHOPLIFT
#endif
#include "../../src/machines/system1/system1_collision.cpp"  // before Z80.h: its `word` macro
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#define private public
#define protected public
#include "../../src/machines/system1/system1.cpp"
#ifdef ENABLE_CHOPLIFT
#include "../../src/machines/choplift/choplift.cpp"
#endif
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
static uint8_t opcodeRom[0x8000];
static uint8_t bankOpcodeRom[4 * 0x4000];

static constexpr FlashAsset<unsigned char, PLAIN> F_MAIN = {mainRom, sizeof(mainRom)};
static constexpr FlashAsset<unsigned char, PLAIN> F_SOUND = {soundRom, sizeof(soundRom)};
static constexpr FlashAsset<uint32_t[8], PLAIN> F_TILES = {tileGfx, 1};
static constexpr FlashAsset<unsigned short, PLAIN> F_TILE_MAP = {tileMap, 1};
static constexpr FlashAsset<unsigned char, PLAIN> F_SPRITES = {spriteRom, sizeof(spriteRom)};
static constexpr FlashAsset<unsigned char, PLAIN> F_LOOKUP = {lookupProm, sizeof(lookupProm)};
static constexpr FlashAsset<unsigned char, PLAIN> F_BANKS = {bankRom, sizeof(bankRom)};
static constexpr FlashAsset<unsigned char, PLAIN> F_OPCODES = {opcodeRom, sizeof(opcodeRom)};
static constexpr FlashAsset<unsigned char, PLAIN> F_BANK_OPCODES = {bankOpcodeRom,
                                                                     sizeof(bankOpcodeRom)};

class fake : public system1 {
public:
  fake(Board board, Bank bank)
      : system1(Roms{F_MAIN, F_SOUND, F_TILES, F_TILE_MAP, F_SPRITES, F_LOOKUP, nullptr, F_BANKS, fakePalette},
                Config{system1_video::VISAREA_FULL, true, board, ROT0,
                       {8, 8, system1_video::NO_PLAYER, 0, 8}, bank}) {}
  signed char machineType() override { return MCH_MENU; }

protected:
  fake(Board board, Vram vram)
      : system1(Roms{F_MAIN, F_SOUND, F_TILES, F_TILE_MAP, F_SPRITES, F_LOOKUP, F_OPCODES, F_BANKS,
                     fakePalette, F_BANK_OPCODES},
                Config{system1_video::VISAREA_FULL, true, board, ROT90, {}, BANK_0C,
                       BUTTONS_GENERIC, vram}) {}
  friend void bootPaged();

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

static Input input;
static unsigned short frame_buffer[256 * 8];
static sprite_S sprite_buf[128];
static unsigned char memory[RAMSIZE];

static constexpr uint16_t PPI_B_DATA = 0x15;  // video mode, bank bits
static constexpr uint16_t PPI_C_DATA = 0x16;  // sound control
static constexpr uint16_t PORT_P1 = 0x00;  // PORT_SYSTEM: system1.cpp

static void boot(system1::Board board, system1::Bank bank) {
  delete machine;
  machine = new fake(board, bank);
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->current_cpu = system1::MAIN_CPU;
}

// tokisens board: MC-8123 opcodes for all banks, 8 VRAM pages
void bootPaged() {
  delete machine;
  machine = new fake(system1::BOARD_SYS2_FIXED, system1::VRAM_PAGED);
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
  memset(opcodeRom, 0xa5, sizeof(opcodeRom));
  for (uint32_t i = 0; i < sizeof(bankOpcodeRom); i++) {
    bankOpcodeRom[i] = (uint8_t)(0xc0 + i / 0x4000);  // byte = 0xc0 + bank number
  }
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

// MAME banked_decrypted_opcodes_map: bank1d follows the data bank (bank0c)
static void test_bank_opcodes_follow_bank(void) {
  bootPaged();
  TEST_ASSERT_EQUAL_UINT8(0xa5, machine->opZ80(0x0000));
  TEST_ASSERT_EQUAL_UINT8(0x5a, machine->rdZ80(0x0000));
  for (int e = 0; e < 4; e++) {
    machine->outZ80(PPI_B_DATA, e << 2);
    TEST_ASSERT_EQUAL_UINT8(0xc0 + e, machine->opZ80(0x8000));
    TEST_ASSERT_EQUAL_UINT8(0xc0 + e, machine->opPrefixZ80(0xbfff));
    TEST_ASSERT_EQUAL_UINT8(0xb0 + e, machine->rdZ80(0x8000));
  }
}

// MAME videoram_r/w: e000-efff = pages 2n, 2n+1, n = (port C >> 1) % 4
static void test_paged_vram_window(void) {
  bootPaged();
  for (int n = 0; n < 4; n++) {
    machine->outZ80(PPI_C_DATA, 0x01 | (n << 1));
    machine->wrZ80(0xe000, 0x10 + n);
    machine->wrZ80(0xe800, 0x20 + n);
  }
  for (int n = 0; n < 4; n++) {
    TEST_ASSERT_EQUAL_UINT8(0x10 + n, machine->vramPages[(2 * n) * system1_video::PAGE_BYTES]);
    TEST_ASSERT_EQUAL_UINT8(0x20 + n, machine->vramPages[(2 * n + 1) * system1_video::PAGE_BYTES]);
    machine->outZ80(PPI_C_DATA, 0x01 | (n << 1));
    TEST_ASSERT_EQUAL_UINT8(0x10 + n, machine->rdZ80(0xe000));
  }
}

// MAME screen_update_system2: bg pages videoram[0x740 + 2p] & 7, one x
// scroll from 0x7c0 for all rows, y scroll 0x7ba; registers live in page 0
static void test_paged_layers(void) {
  bootPaged();
  machine->outZ80(PPI_C_DATA, 0x01);
  const uint8_t sel[4] = {0x0d, 0x03, 0x14, 0x05};
  for (int p = 0; p < 4; p++) {
    machine->wrZ80(0xe740 + 2 * p, sel[p]);
  }
  machine->wrZ80(0xe7c0, 0x34);
  machine->wrZ80(0xe7c1, 0x01);
  machine->wrZ80(0xe7c2, 0x99);  // row 1 register: ignored without row scroll
  machine->wrZ80(0xe7ba, 0x42);
  machine->outZ80(PPI_C_DATA, 0x03);
  machine->wrZ80(0xe740, 0x07);  // page 2: not the register page

  machine->startVblank();
  machine->prepare_frame();
  const uint8_t want[4] = {5, 3, 4, 5};
  for (int p = 0; p < 4; p++) {
    TEST_ASSERT_EQUAL_UINT8(want[p], machine->video.layers.bgPage[p]);
  }
  for (int r = 0; r < system1_video::SCROLL_ROWS; r++) {
    TEST_ASSERT_EQUAL_INT16(0x134 - 512 + 10, machine->video.layers.rowScroll[r]);
  }
  TEST_ASSERT_EQUAL_UINT8(0x42, machine->video.layers.scrollY);
}

// bg pages render from the copy latched at vblank, not live VRAM
static void test_paged_bg_snapshot(void) {
  bootPaged();
  machine->outZ80(PPI_C_DATA, 0x01);
  for (int p = 0; p < 4; p++) {
    machine->wrZ80(0xe740 + 2 * p, 3);
  }
  machine->outZ80(PPI_C_DATA, 0x03);
  machine->wrZ80(0xe800, 0x11);  // page 3, first byte

  machine->startVblank();
  machine->prepare_frame();
  machine->wrZ80(0xe800, 0x22);
  const system1_video::LineSrc f = machine->lineSrc();
  for (int p = 0; p < 4; p++) {
    TEST_ASSERT_EQUAL_UINT8(0x11, f.bgPages[p][0]);
  }
  TEST_ASSERT_EQUAL_PTR(machine->vramPages, f.fgPage);
}

#ifdef ENABLE_CHOPLIFT
static void bootChoplift() {
  delete machine;
  machine = nullptr;
  Arena::reserve(&choplift_arena_plan);
  machine = new choplift();
  machine->init(&input, frame_buffer, sprite_buf, memory);
  machine->reset();
  machine->start();
  machine->start();
}

// real ROMs: banked code at 8000-bfff, PPI sound latch, inverted mute
// (the game writes port C 0x01 for sound on). Its RAM / VRAM test runs
// from bank 3 until frame ~4150 here (MAME ~2700: the engine steps
// instructions, not cycles), as Heavy Metal. Coin + start every 10 s as
// test_system1_boot.
static void test_choplift_boots(void) {
  bootChoplift();
  int peak = 0;
  for (int frame = 0; frame < 4800; frame++) {
    keys = frame % 600 >= 300 && frame % 600 < 310 ? BUTTON_COIN : 0;
    keys |= frame % 600 >= 400 && frame % 600 < 410 ? BUTTON_START : 0;
    machine->run_frame();
    for (int c = 0; c < 6; c++) {
      const int p = audioPeak();
      peak = p > peak ? p : peak;
    }
  }
  keys = 0;

  int page0 = 0;
  for (int i = 0; i < system1_video::PAGE_BYTES; i++) {
    page0 += memory[system1::VIDEO_RAM + i] != 0;
  }
  TEST_ASSERT_TRUE(machine->game_started);
  TEST_ASSERT_GREATER_THAN(0, page0);
  TEST_ASSERT_GREATER_THAN(0, peak);
}

// MAME choplift: BUTTON1 (shoot) on P1 bit 2, BUTTON2 (turn) on bit 1;
// START (and galagino B) turns, START also starts from the SYSTEM port
static void test_choplift_inputs(void) {
  bootChoplift();
  keys = BUTTON_FIRE;
  TEST_ASSERT_EQUAL_HEX8(0xfb, machine->inZ80(PORT_P1));
  keys = START_KEY;
  TEST_ASSERT_EQUAL_HEX8(0xfd, machine->inZ80(PORT_P1));
  keys = BUTTON_B;
  TEST_ASSERT_EQUAL_HEX8(0xfd, machine->inZ80(PORT_P1));
  keys = BUTTON_START;
  TEST_ASSERT_EQUAL_HEX8(0xef, machine->inZ80(PORT_SYSTEM));
  keys = 0;
}

// MAME choplift swaps the DIP banks: port 0x0c ("SWA") holds the game
// options, ports 0x0d / 0x10 ("SWB") the coinage
static void test_choplift_dips(void) {
  bootChoplift();
  TEST_ASSERT_EQUAL_HEX8(0xdc, machine->inZ80(0x0c));
  TEST_ASSERT_EQUAL_HEX8(0xff, machine->inZ80(0x0d));
  TEST_ASSERT_EQUAL_HEX8(0xff, machine->inZ80(0x10));
}
#endif

int main(int, char **) {
  Arena::init();
  UNITY_BEGIN();
  RUN_TEST(test_bank0c_select);
  RUN_TEST(test_sys2_mute_inverted);
  RUN_TEST(test_sys1_ppi_never_muted);
  RUN_TEST(test_bank_opcodes_follow_bank);
  RUN_TEST(test_paged_vram_window);
  RUN_TEST(test_paged_layers);
  RUN_TEST(test_paged_bg_snapshot);
#ifdef ENABLE_CHOPLIFT
  RUN_TEST(test_choplift_boots);
  RUN_TEST(test_choplift_inputs);
  RUN_TEST(test_choplift_dips);
#endif
  return UNITY_END();
}
