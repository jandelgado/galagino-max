#ifdef ENABLE_PITFALL2

#include "pitfall2.h"
#include "pitfall2_logo.h"
#include "pitfall2_main_rom.h"
#include "pitfall2_sound_rom.h"
#include "pitfall2_tiles.h"
#include "pitfall2_tile_map.h"
#include "pitfall2_sprites.h"
#include "pitfall2_lookup_prom.h"

// Z80 PIO board (sys1pio), ROT0, full 256 px visarea. All content lies in
// x 5..249 (MAME), so the 240 px window moves within 5..10,
// following Harry (sprite slot 0). The HUD lines (1UP, energy, timer,
// scores) stay at x 5: "1UP" starts at x 5.
// FIRE jumps (BUTTON2 does the same in MAME).
static const system1_video::Window PITFALL2_WINDOW = {5, 10, 0, 24, 5};

pitfall2::pitfall2() : system1(Roms{
    pitfall2_main_rom_blob, pitfall2_sound_rom_blob, pitfall2_tiles_blob, pitfall2_tile_map_blob,
    pitfall2_sprites_blob, pitfall2_lookup_prom_blob,
}, Config{system1_video::VISAREA_FULL, false, BOARD_PIO, ROT0, PITFALL2_WINDOW}) { }

const Logo &pitfall2::logo(void) {
  return pitfall2_logo;
}

#ifdef LED_PIN
// from logos/pitfall2.png: orange letters, dark red outline
static const CRGB PITFALL2_LED_A = CRGB(0xffaa00);
static const CRGB PITFALL2_LED_B = CRGB(0xde7e00);
static const CRGB PITFALL2_LED_C = CRGB(0x961e00);
static const CRGB PITFALL2_LED_HIT = CRGB(0xffffff);

void pitfall2::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      PITFALL2_LED_C, PITFALL2_LED_B, PITFALL2_LED_A, PITFALL2_LED_A,
      PITFALL2_LED_A, PITFALL2_LED_B, PITFALL2_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void pitfall2::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, PITFALL2_LED_HIT);
}
#endif

#endif // ENABLE_PITFALL2
