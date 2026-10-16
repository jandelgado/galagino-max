#ifdef ENABLE_SWAT

#include "swat.h"
#include "swat_logo.h"
#include "swat_main_rom.h"
#include "swat_opcodes.h"
#include "swat_sound_rom.h"
#include "swat_tiles.h"
#include "swat_tile_map.h"
#include "swat_sprites.h"
#include "swat_lookup_prom.h"

// full 256 px visarea (sys1ppi); FIRE shoots, START throws a bomb.
// 315-5048: romconv stores the data-decrypted ROM as main_rom and the
// opcode-decrypted 0000-7fff as opcodes
swat::swat() : system1(Roms{
    swat_main_rom_blob, swat_sound_rom_blob, swat_tiles_blob, swat_tile_map_blob,
    swat_sprites_blob, swat_lookup_prom_blob, swat_opcodes_blob,
}, Config{system1_video::VISAREA_FULL, true}) { }

const Logo &swat::logo(void) {
  return swat_logo;
}

#ifdef LED_PIN
// logo colors: title cyan and magenta, gold frame
static const CRGB SWAT_LED_A = CRGB(0x48e7f6);
static const CRGB SWAT_LED_B = CRGB(0x9100b6);
static const CRGB SWAT_LED_C = CRGB(0xdccd7f);
static const CRGB SWAT_LED_HIT = CRGB(0xffffff);

void swat::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      SWAT_LED_C, SWAT_LED_B, SWAT_LED_A, SWAT_LED_A,
      SWAT_LED_A, SWAT_LED_B, SWAT_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void swat::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, SWAT_LED_HIT);
}
#endif

#endif // ENABLE_SWAT
