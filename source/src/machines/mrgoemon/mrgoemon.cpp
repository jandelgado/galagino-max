// Mr. Goemon (Konami 1986), MAME konami/gberet.cpp mrgoemon_state.
// Green Beret board plus 8 x 2 KB ROM banks at f800-ffff (ROM c000-ffff).
// gberet::opZ80 falls through to the virtual rdZ80, which covers the window.
#include "../../catalog.h"
#ifdef GAME_MRGOEMON

#include "mrgoemon.h"
#include "mrgoemon_logo.h"
#include "mrgoemon_dipswitches.h"
#include "mrgoemon_rom.h"
#include "mrgoemon_tilemap.h"
#include "mrgoemon_spritemap.h"
#include "mrgoemon_cmap.h"

mrgoemon::mrgoemon()
  : gberet(Assets{mrgoemon_rom_blob, mrgoemon_tilemap_blob, mrgoemon_spritemap_blob,
                  mrgoemon_tile_cmap, mrgoemon_tile_transp, mrgoemon_spr_cmap,
                  mrgoemon_spr_transp, MRGOEMON_DSW2}) { }

void mrgoemon::reset() {
  bank_base = 0xc000;
  gberet::reset();
}

unsigned char mrgoemon::rdZ80(unsigned short Addr) {
  if (Addr >= 0xf800) { return rom_ptr[bank_base + (Addr - 0xf800)]; }
  return gberet::rdZ80(Addr);
}

void mrgoemon::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr == 0xf000) {
    bank_base = 0xc000 + (Value >> 5) * 0x800;  // b5-7 bank; b0-1 coin counters ignored
    return;
  }
  gberet::wrZ80(Addr, Value);
}

const Logo &mrgoemon::logo(void) {
  return mrgoemon_logo;
}

#ifdef LED_PIN
// from logos/mrgoemon.png: red letters, cream shadow, dark red
static const CRGB MRGOEMON_LED_RED = CRGB(0xd61e1e);
static const CRGB MRGOEMON_LED_CREAM = CRGB(0xf5e491);
static const CRGB MRGOEMON_LED_DARK = CRGB(0x74161a);

void mrgoemon::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      MRGOEMON_LED_DARK, MRGOEMON_LED_RED, MRGOEMON_LED_CREAM, MRGOEMON_LED_CREAM,
      MRGOEMON_LED_CREAM, MRGOEMON_LED_RED, MRGOEMON_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// blink red while the pipe (FIRE) is held, else the menu colors
void mrgoemon::gameLeds(CRGB *leds) {
  fireBlinkLeds(leds, input, led_frame, MRGOEMON_LED_RED, menuLeds);
}
#endif

#endif // GAME_MRGOEMON
