#include "../../catalog.h"
#ifdef GAME_RAFLESIA

#include "raflesia.h"
#include "raflesia_logo.h"
#include "raflesia_main_rom.h"
#include "raflesia_sound_rom.h"
#include "raflesia_tiles.h"
#include "raflesia_tile_map.h"
#include "raflesia_sprites.h"
#include "raflesia_lookup_prom.h"

// full 256 px visarea (sys1ppi); one button: FIRE shoots
raflesia::raflesia() : system1(Roms{
    raflesia_main_rom_blob, raflesia_sound_rom_blob, raflesia_tiles_blob, raflesia_tile_map_blob,
    raflesia_sprites_blob, raflesia_lookup_prom_blob,
}, Config{system1_video::VISAREA_FULL, false}) { }

const Logo &raflesia::logo(void) {
  return raflesia_logo;
}

#ifdef LED_PIN
// from logos/raflesia.png
static const CRGB RAFLESIA_LED_A = CRGB(0xffb600);
static const CRGB RAFLESIA_LED_B = CRGB(0xff6e00);
static const CRGB RAFLESIA_LED_C = CRGB(0xb60000);

void raflesia::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      RAFLESIA_LED_C, RAFLESIA_LED_B, RAFLESIA_LED_A, RAFLESIA_LED_A,
      RAFLESIA_LED_A, RAFLESIA_LED_B, RAFLESIA_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// all yellow while the collision hardware reports a hit, else the menu colors
void raflesia::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, RAFLESIA_LED_A);
}
#endif

#endif // GAME_RAFLESIA
