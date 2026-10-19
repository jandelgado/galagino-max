#ifdef ENABLE_FOURDWARRIO

#include "fourdwarrio.h"
#include "fourdwarrio_logo.h"
#include "fourdwarrio_main_rom.h"
#include "fourdwarrio_opcodes.h"
#include "fourdwarrio_sound_rom.h"
#include "fourdwarrio_tiles.h"
#include "fourdwarrio_tile_map.h"
#include "fourdwarrio_sprites.h"
#include "fourdwarrio_lookup_prom.h"

// Z80 PIO board (sys1pio), ROT0, full 256 px visarea. The scrolling
// playfield fills x 0..255 and the ship flies across it: the window stays
// centered at 8, cropping 8 px per side. FIRE is BUTTON1, START BUTTON2.
// 315-5162 (segacrp2): romconv decrypts; the opcode image stays PLAIN in
// flash (Arena total 218656 > 201500).
static const system1_video::Window FOURDWARRIO_WINDOW = {8, 8, system1_video::NO_PLAYER, 0, 0};

fourdwarrio::fourdwarrio() : system1(Roms{
    fourdwarrio_main_rom_blob, fourdwarrio_sound_rom_blob, fourdwarrio_tiles_blob, fourdwarrio_tile_map_blob,
    fourdwarrio_sprites_blob, fourdwarrio_lookup_prom_blob, fourdwarrio_opcodes_blob,
}, Config{system1_video::VISAREA_FULL, true, BOARD_PIO, ROT0, FOURDWARRIO_WINDOW}) { }

const Logo &fourdwarrio::logo(void) {
  return fourdwarrio_logo;
}

#ifdef LED_PIN
// from logos/fourdwarrio.png: teal letters, cyan shade, dark blue edge
static const CRGB FOURDWARRIO_LED_A = CRGB(0x00dca8);
static const CRGB FOURDWARRIO_LED_B = CRGB(0x0b88b9);
static const CRGB FOURDWARRIO_LED_C = CRGB(0x000094);
static const CRGB FOURDWARRIO_LED_HIT = CRGB(0xffffff);

void fourdwarrio::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      FOURDWARRIO_LED_C, FOURDWARRIO_LED_B, FOURDWARRIO_LED_A, FOURDWARRIO_LED_A,
      FOURDWARRIO_LED_A, FOURDWARRIO_LED_B, FOURDWARRIO_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void fourdwarrio::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, FOURDWARRIO_LED_HIT);
}
#endif

#endif // ENABLE_FOURDWARRIO
