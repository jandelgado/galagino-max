#include "../../catalog.h"
#ifdef GAME_GARDIA

#include "gardia.h"
#include "gardia_logo.h"
#include "gardia_main_rom.h"
#include "gardia_opcodes.h"
#include "gardia_sound_rom.h"
#include "gardia_tiles.h"
#include "gardia_tile_map.h"
#include "gardia_sprites.h"
#include "gardia_lookup_prom.h"
#include "gardia_banks.h"
#include "gardia_palette.h"

// Z80 PIO board (sys1pio), ROT270, full 256 px visarea, Heavy Metal
// banking (MAME bank44) and color PROM palette. Sprites and the opcode
// image stay PLAIN in flash. FIRE shoots, START is button 2.
gardia::gardia() : system1(Roms{
    gardia_main_rom_blob, gardia_sound_rom_blob, gardia_tiles_blob, gardia_tile_map_blob,
    gardia_sprites_blob, gardia_lookup_prom_blob, gardia_opcodes_blob,
    gardia_banks_blob, gardia_palette,
}, Config{system1_video::VISAREA_FULL, true, BOARD_PIO, ROT270, {}, BANK_44}) {
  // MAME sys1piox_317_0006: second SN76489A at SOUND_CLOCK / 4
  sn[1] = Sn76489(Sn76489::INC_2MHZ);
}

const Logo &gardia::logo(void) {
  return gardia_logo;
}

#ifdef LED_PIN
// from logos/gardia.png: pale yellow letters, grey shade
static const CRGB GARDIA_LED_A = CRGB(0xf4f4bf);
static const CRGB GARDIA_LED_B = CRGB(0xccd3c0);
static const CRGB GARDIA_LED_C = CRGB(0x888f8b);
static const CRGB GARDIA_LED_HIT = CRGB(0xff4000);

void gardia::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      GARDIA_LED_C, GARDIA_LED_B, GARDIA_LED_A, GARDIA_LED_A,
      GARDIA_LED_A, GARDIA_LED_B, GARDIA_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// orange while the collision hardware reports a hit, else the menu colors
void gardia::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, GARDIA_LED_HIT);
}
#endif

#endif // GAME_GARDIA
