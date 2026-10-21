#include "../../catalog.h"
#ifdef GAME_IMSORRY

#include "imsorry.h"
#include "imsorry_logo.h"
#include "imsorry_main_rom.h"
#include "imsorry_opcodes.h"
#include "imsorry_sound_rom.h"
#include "imsorry_tiles.h"
#include "imsorry_tile_map.h"
#include "imsorry_sprites.h"
#include "imsorry_lookup_prom.h"

// Z80 PIO board (sys1pio), ROT0, full 256 px visarea. All content lies in
// x 8..247 (MAME): the window stays at 8. FIRE is BUTTON1,
// START BUTTON2.
static const system1_video::Window IMSORRY_WINDOW = {8, 8, system1_video::NO_PLAYER, 0, 0};

imsorry::imsorry() : system1(Roms{
    imsorry_main_rom_blob, imsorry_sound_rom_blob, imsorry_tiles_blob, imsorry_tile_map_blob,
    imsorry_sprites_blob, imsorry_lookup_prom_blob, imsorry_opcodes_blob,
}, Config{system1_video::VISAREA_FULL, true, BOARD_PIO, ROT0, IMSORRY_WINDOW}) { }

const Logo &imsorry::logo(void) {
  return imsorry_logo;
}

#ifdef LED_PIN
// from logos/imsorry.png: red letters, yellow highlight, dark red outline
static const CRGB IMSORRY_LED_A = CRGB(0xde0000);
static const CRGB IMSORRY_LED_B = CRGB(0xf5a93f);
static const CRGB IMSORRY_LED_C = CRGB(0x540004);
static const CRGB IMSORRY_LED_HIT = CRGB(0xffffff);

void imsorry::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      IMSORRY_LED_C, IMSORRY_LED_B, IMSORRY_LED_A, IMSORRY_LED_A,
      IMSORRY_LED_A, IMSORRY_LED_B, IMSORRY_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void imsorry::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, IMSORRY_LED_HIT);
}
#endif

#endif // GAME_IMSORRY
