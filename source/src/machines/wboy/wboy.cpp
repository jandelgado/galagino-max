#include "../../catalog.h"
#ifdef GAME_WBOY

#include "wboy.h"
#include "wboy_logo.h"
#include "wboy_main_rom.h"
#include "wboy_sound_rom.h"
#include "wboy_tiles.h"
#include "wboy_tile_map.h"
#include "wboy_sprites.h"
#include "wboy_lookup_prom.h"

// 8255 PPI board (sys1ppi), ROT0, full 256 px visarea. All content lies in
// x 16..239 (MAME): the window stays at 8. P1 bit 2 (MAME
// BUTTON2) on FIRE jumps, bit 1 (BUTTON1) on START attacks (MAME probe).
// 2-way stick.
static const system1_video::Window WBOY_WINDOW = {8, 8, system1_video::NO_PLAYER, 0, 0};

wboy::wboy() : system1(Roms{
    wboy_main_rom_blob, wboy_sound_rom_blob, wboy_tiles_blob, wboy_tile_map_blob,
    wboy_sprites_blob, wboy_lookup_prom_blob,
}, Config{system1_video::VISAREA_FULL, true, BOARD_PPI, ROT0, WBOY_WINDOW}) { }

const Logo &wboy::logo(void) {
  return wboy_logo;
}

#ifdef LED_PIN
// from logos/wboy.png: red letters, pink face, dark red shade
static const CRGB WBOY_LED_A = CRGB(0xef0000);
static const CRGB WBOY_LED_B = CRGB(0xf296a8);
static const CRGB WBOY_LED_C = CRGB(0x6d0000);
static const CRGB WBOY_LED_HIT = CRGB(0xffffff);

void wboy::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      WBOY_LED_C, WBOY_LED_B, WBOY_LED_A, WBOY_LED_A,
      WBOY_LED_A, WBOY_LED_B, WBOY_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void wboy::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, WBOY_LED_HIT);
}
#endif

#endif // GAME_WBOY
