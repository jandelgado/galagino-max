#include "../../catalog.h"
#ifdef GAME_HVYMETAL

#include "hvymetal.h"
#include "hvymetal_logo.h"
#include "hvymetal_main_rom.h"
#include "hvymetal_opcodes.h"
#include "hvymetal_sound_rom.h"
#include "hvymetal_tiles.h"
#include "hvymetal_tile_map.h"
#include "hvymetal_sprites.h"
#include "hvymetal_lookup_prom.h"
#include "hvymetal_banks.h"
#include "hvymetal_palette.h"

// Z80 PIO board (sys1pio), ROT0, full 256 px visarea. The status panel
// sits at x 0..63 with labels from x 0, the playfield runs to x 255
// (MAME): the window stays at 0, cropping 16 px of playfield. 4 banks of
// 16 KB at 8000-bfff (MAME bank44), palette from the color PROMs.
// Sprites, banks and the opcode image stay PLAIN in flash. MAME BUTTON1
// sits on P1 bit 1: BUTTONS_SWAPPED puts it on FIRE, BUTTON2 on START.
static const system1_video::Window HVYMETAL_WINDOW = {0, 0, system1_video::NO_PLAYER, 0, 0};

hvymetal::hvymetal() : system1(Roms{
    hvymetal_main_rom_blob, hvymetal_sound_rom_blob, hvymetal_tiles_blob, hvymetal_tile_map_blob,
    hvymetal_sprites_blob, hvymetal_lookup_prom_blob, hvymetal_opcodes_blob,
    hvymetal_banks_blob, hvymetal_palette,
}, Config{system1_video::VISAREA_FULL, true, BOARD_PIO, ROT0, HVYMETAL_WINDOW, BANK_44, BUTTONS_SWAPPED}) { }

// Every power-on (0000 -> 4a3a): PIO init (4b77), screen setup, then a
// RAM test and ROM checksum, 47 s on the real board. Skip both: leave RAM
// as the test's last pass does, run the PIO init and return from it to
// the test's exit (4ab6). RAM 1200 frames later equals a full boot.
static const uint16_t TESTED_RAM[][2] = {{0xc000, 0xd000}, {0xd800, 0xf000}};  // [from, to)
static const uint16_t PIO_INIT = 0x4b77;
static const uint16_t TEST_PASSED = 0x4ab6;
static const uint16_t STACK_TOP = 0xd000;
static const int MAIN_Z80 = 0;

void hvymetal::reset() {
  system1::reset();
  current_cpu = MAIN_Z80;
  for (const auto &range : TESTED_RAM) {
    for (uint32_t a = range[0]; a < range[1]; a++) {
      wrZ80(a, a - 1);  // last pattern: byte n of a page = n - 1
    }
  }

  Z80 &z80 = cpu[MAIN_Z80];
  z80.SP.W = STACK_TOP - 2;
  wrZ80(z80.SP.W, TEST_PASSED & 0xff);
  wrZ80(z80.SP.W + 1, TEST_PASSED >> 8);
  z80.PC.W = PIO_INIT;
}

const Logo &hvymetal::logo(void) {
  return hvymetal_logo;
}

#ifdef LED_PIN
// from logos/hvymetal.png: blue letters, lavender highlight, dark blue edge
static const CRGB HVYMETAL_LED_A = CRGB(0x5751b2);
static const CRGB HVYMETAL_LED_B = CRGB(0xb6b0e8);
static const CRGB HVYMETAL_LED_C = CRGB(0x030365);
static const CRGB HVYMETAL_LED_HIT = CRGB(0xffffff);

void hvymetal::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      HVYMETAL_LED_C, HVYMETAL_LED_B, HVYMETAL_LED_A, HVYMETAL_LED_A,
      HVYMETAL_LED_A, HVYMETAL_LED_B, HVYMETAL_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void hvymetal::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, HVYMETAL_LED_HIT);
}
#endif

#endif // GAME_HVYMETAL
