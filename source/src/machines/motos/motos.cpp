#include "../../catalog.h"
#ifdef GAME_MOTOS

#include "motos.h"
#include "motos_logo.h"
#include "motos_rom_main.h"
#include "motos_rom_sub.h"
#include "motos_tilemap.h"
#include "motos_spritemap.h"
#include "motos_cmap.h"
#include "motos_wavetable.h"

motos::motos()
  : digdug2(Board{motos_colormap_sprites_blob, motos_rom_main_blob, motos_rom_sub_blob,
                  motos_sprites_blob, motos_tilemap_blob, motos_colormap_tiles,
                  motos_colormap_tiles_prio, motos_wavetable, IO_56XX}) { }

const Logo &motos::logo(void) {
  return motos_logo;
}

#ifdef LED_PIN
// logo colors (logos/motos.png): steel letters, dark blue shade
static const CRGB MOTOS_LED_BLUE(0x90a8c0), MOTOS_LED_DARK(0x202c60);

void motos::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      MOTOS_LED_DARK, MOTOS_LED_BLUE, MOTOS_LED_BLUE, MOTOS_LED_BLUE,
      MOTOS_LED_BLUE, MOTOS_LED_BLUE, MOTOS_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while button 2 (jump) is held
void motos::gameLeds(CRGB *leds) {
  if (!drill_active) {
    menuLeds(leds);
    return;
  }
  for (unsigned char c = 0; c < NUM_LEDS; c++) {
    leds[c] = CRGB(0xffffff);
  }
}
#endif

#endif
