#include "../../catalog.h"
#ifdef GAME_TOKISENS

#include "tokisens.h"
#include "tokisens_logo.h"
#include "tokisens_main_rom.h"
#include "tokisens_sound_rom.h"
#include "tokisens_tiles.h"
#include "tokisens_tile_map.h"
#include "tokisens_sprites.h"
#include "tokisens_lookup_prom.h"
#include "tokisens_opcodes.h"
#include "tokisens_banks.h"
#include "tokisens_bank_opcodes.h"
#include "tokisens_palette.h"

// System 2 board (MAME sys2xb): PPI, inverted mute bit, one x scroll, 4
// banks of 16 KB at 8000-bfff (bank0c) with their MC-8123 opcodes, all 8
// VRAM pages in the Arena. ROT90, full 256 px visarea: panel rows 16..271.
// FIRE shoots, START (or B) is button 2. MAME runs the main CPU 1.07x
// fast (init_tokisens) so the attract player survives; here, as in MAME
// without it, the demo ends in CONTINUE / GAME OVER at ~107 s, then the
// title. 24 instead of 22 steps per line did not change that.
tokisens::tokisens() : system1(Roms{
    tokisens_main_rom_blob, tokisens_sound_rom_blob, tokisens_tiles_blob, tokisens_tile_map_blob,
    tokisens_sprites_blob, tokisens_lookup_prom_blob, tokisens_opcodes_blob,
    tokisens_banks_blob, tokisens_palette, tokisens_bank_opcodes_blob,
}, Config{system1_video::VISAREA_FULL, true, BOARD_SYS2_FIXED, ROT90, {}, BANK_0C,
          BUTTONS_GENERIC, VRAM_PAGED}) { }

const Logo &tokisens::logo(void) {
  return tokisens_logo;
}

#ifdef LED_PIN
// from logos/tokisens.png: tan letters, brown shade, green subtitle
static const CRGB TOKISENS_LED_A = CRGB(0xf1d28f);
static const CRGB TOKISENS_LED_B = CRGB(0xae8f43);
static const CRGB TOKISENS_LED_C = CRGB(0x624300);
static const CRGB TOKISENS_LED_HIT = CRGB(0x8fae43);

void tokisens::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      TOKISENS_LED_C, TOKISENS_LED_B, TOKISENS_LED_A, TOKISENS_LED_A,
      TOKISENS_LED_A, TOKISENS_LED_B, TOKISENS_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// green while the collision hardware reports a hit, else the menu colors
void tokisens::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, TOKISENS_LED_HIT);
}
#endif

#endif // GAME_TOKISENS
