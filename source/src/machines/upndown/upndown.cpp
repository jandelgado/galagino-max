#ifdef ENABLE_UPNDOWN

#include "upndown.h"
#include "upndown_logo.h"
#include "upndown_main_rom.h"
#include "upndown_sound_rom.h"
#include "upndown_tiles.h"
#include "upndown_tile_map.h"
#include "upndown_sprites.h"
#include "upndown_lookup_prom.h"

// full 256 px visarea (sys1ppi); one button: FIRE jumps
upndown::upndown() : system1(Roms{
    upndown_main_rom_blob, upndown_sound_rom_blob, upndown_tiles_blob, upndown_tile_map_blob,
    upndown_sprites_blob, upndown_lookup_prom_blob,
}, Config{system1_video::VISAREA_FULL, false}) { }

const Logo &upndown::logo(void) {
  return upndown_logo;
}

#ifdef LED_PIN
// from logos/upndown.png: yellow letters, green outline, navy shadow
static const CRGB UPNDOWN_LED_A = CRGB(0xedcb0b);
static const CRGB UPNDOWN_LED_B = CRGB(0x157360);
static const CRGB UPNDOWN_LED_C = CRGB(0x192049);
static const CRGB UPNDOWN_LED_HIT = CRGB(0xffffff);

void upndown::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      UPNDOWN_LED_C, UPNDOWN_LED_B, UPNDOWN_LED_A, UPNDOWN_LED_A,
      UPNDOWN_LED_A, UPNDOWN_LED_B, UPNDOWN_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void upndown::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, UPNDOWN_LED_HIT);
}
#endif

#endif // ENABLE_UPNDOWN
