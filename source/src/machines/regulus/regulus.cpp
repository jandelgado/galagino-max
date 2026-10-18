#include "../../catalog.h"
#ifdef GAME_REGULUS

#include "regulus.h"
#include "regulus_logo.h"
#include "regulus_main_rom.h"
#include "regulus_sound_rom.h"
#include "regulus_tiles.h"
#include "regulus_tile_map.h"
#include "regulus_sprites.h"
#include "regulus_lookup_prom.h"

// full 256 px visarea (sys1ppi, HUD at both ends); FIRE fires laser, START
// drops bomb
regulus::regulus() : system1(Roms{
    regulus_main_rom_blob, regulus_sound_rom_blob, regulus_tiles_blob, regulus_tile_map_blob,
    regulus_sprites_blob, regulus_lookup_prom_blob,
}, Config{system1_video::VISAREA_FULL, true}) { }

const Logo &regulus::logo(void) {
  return regulus_logo;
}

#ifdef LED_PIN
// logo colors: yellow letters, orange and red outline
static const CRGB REGULUS_LED_A = CRGB(0xffff68);
static const CRGB REGULUS_LED_B = CRGB(0xf07020);
static const CRGB REGULUS_LED_C = CRGB(0xf01000);
static const CRGB REGULUS_LED_HIT = CRGB(0xffffff);

void regulus::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      REGULUS_LED_C, REGULUS_LED_B, REGULUS_LED_A, REGULUS_LED_A,
      REGULUS_LED_A, REGULUS_LED_B, REGULUS_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void regulus::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, REGULUS_LED_HIT);
}
#endif

#endif // GAME_REGULUS
