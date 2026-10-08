#ifdef ENABLE_STARJACK

#include "starjack.h"
#include "starjack_logo.h"
#include "starjack_main_rom.h"
#include "starjack_sound_rom.h"
#include "starjack_tiles.h"
#include "starjack_sprites.h"
#include "starjack_lookup_prom.h"

starjack::starjack() : system1(Roms{
    starjack_main_rom_blob, starjack_sound_rom_blob, starjack_tiles_blob,
    starjack_sprites_blob, starjack_lookup_prom_blob,
}) { }

const Logo &starjack::logo(void) {
  return starjack_logo;
}

#ifdef LED_PIN
// logo colors: cyan "JACKER", white "STAR", dark cyan
static const CRGB STARJACK_LED_CYAN = CRGB(0x00c8dc);
static const CRGB STARJACK_LED_WHITE = CRGB(0xffffff);
static const CRGB STARJACK_LED_DARK = CRGB(0x0098a8);

void starjack::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      STARJACK_LED_DARK, STARJACK_LED_CYAN, STARJACK_LED_CYAN, STARJACK_LED_WHITE,
      STARJACK_LED_CYAN, STARJACK_LED_CYAN, STARJACK_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void starjack::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, STARJACK_LED_WHITE);
}
#endif

#endif // ENABLE_STARJACK
