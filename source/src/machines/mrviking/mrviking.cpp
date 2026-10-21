#include "../../catalog.h"
#ifdef GAME_MRVIKING

#include "mrviking.h"
#include "mrviking_logo.h"
#include "mrviking_main_rom.h"
#include "mrviking_opcodes.h"
#include "mrviking_sound_rom.h"
#include "mrviking_tiles.h"
#include "mrviking_tile_map.h"
#include "mrviking_sprites.h"
#include "mrviking_lookup_prom.h"

// sys1ppis: 240 px visarea (16..495 at 512 px), ROT270. MAME BUTTON1 sits
// on P1 bit 1: FIRE attacks, START presses BUTTON2 (bit 2).
mrviking::mrviking() : system1(Roms{
    mrviking_main_rom_blob, mrviking_sound_rom_blob, mrviking_tiles_blob, mrviking_tile_map_blob,
    mrviking_sprites_blob, mrviking_lookup_prom_blob, mrviking_opcodes_blob,
}, Config{system1_video::VISAREA_SCROLL, true, BOARD_PPI, ROT270, system1_video::Window{},
          BANK_NONE, BUTTONS_SWAPPED}) { }

const Logo &mrviking::logo(void) {
  return mrviking_logo;
}

#ifdef LED_PIN
// from logos/mrviking.png: orange and yellow letters, dark gold shade
static const CRGB MRVIKING_LED_A = CRGB(0xffbd00);
static const CRGB MRVIKING_LED_B = CRGB(0xeee02c);
static const CRGB MRVIKING_LED_C = CRGB(0xc09a1c);
static const CRGB MRVIKING_LED_HIT = CRGB(0xffffff);

void mrviking::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      MRVIKING_LED_C, MRVIKING_LED_B, MRVIKING_LED_A, MRVIKING_LED_A,
      MRVIKING_LED_A, MRVIKING_LED_B, MRVIKING_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void mrviking::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, MRVIKING_LED_HIT);
}
#endif

#endif // GAME_MRVIKING
