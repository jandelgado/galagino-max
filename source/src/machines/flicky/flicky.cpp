#ifdef ENABLE_FLICKY

#include "flicky.h"
#include "flicky_logo.h"
#include "flicky_main_rom.h"
#include "flicky_sound_rom.h"
#include "flicky_tiles.h"
#include "flicky_tile_map.h"
#include "flicky_sprites.h"
#include "flicky_lookup_prom.h"

// Z80 PIO board (sys1pio), ROT0, full 256 px visarea. The game scrolls the
// world and keeps Flicky (sprite slot 8) at x 119..123; all its content
// lies in x 8..247 (MAME), so the window stays at 8.
// One button: FIRE jumps, stick left/right.
static const system1_video::Window FLICKY_WINDOW = {8, 8, system1_video::NO_PLAYER, 0, 0};

flicky::flicky() : system1(Roms{
    flicky_main_rom_blob, flicky_sound_rom_blob, flicky_tiles_blob, flicky_tile_map_blob,
    flicky_sprites_blob, flicky_lookup_prom_blob,
}, Config{system1_video::VISAREA_FULL, false, BOARD_PIO, ROT0, FLICKY_WINDOW}) { }

const Logo &flicky::logo(void) {
  return flicky_logo;
}

#ifdef LED_PIN
// from logos/flicky.png: pink letters, light highlight, dark outline
static const CRGB FLICKY_LED_A = CRGB(0xfbb5fc);
static const CRGB FLICKY_LED_B = CRGB(0xed8be3);
static const CRGB FLICKY_LED_C = CRGB(0x3f1c32);
static const CRGB FLICKY_LED_HIT = CRGB(0xffe7ff);

void flicky::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      FLICKY_LED_C, FLICKY_LED_B, FLICKY_LED_A, FLICKY_LED_A,
      FLICKY_LED_A, FLICKY_LED_B, FLICKY_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// bright while the collision hardware reports a hit, else the menu colors
void flicky::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, FLICKY_LED_HIT);
}
#endif

#endif // ENABLE_FLICKY
