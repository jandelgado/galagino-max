#include "baluba.h"
#include "baluba_dipswitches.h"
#include "baluba_logo.h"
#include "baluba_bg1_tiles.h"
#include "baluba_bg2_tiles.h"
#include "baluba_bg3_tiles.h"
#include "baluba_fg_tiles.h"
#include "baluba_sprites.h"
#include "baluba_main_cpu_rom.h"
#include "baluba_sub_cpu_rom.h"

baluba::baluba() : starforce(Roms{
  baluba_main_cpu_rom, baluba_sub_cpu_rom, baluba_fg_tilemap,
  baluba_bg1_tilemap, baluba_bg2_tilemap, baluba_bg3_tilemap,
  baluba_sprites_16x16, baluba_sprites_32x32,
}) { }

uint8_t baluba::dsw1() {
  return BALUBA_DSW1;
}

uint8_t baluba::dsw2() {
  return BALUBA_DSW2;
}

RomData<unsigned short, COMPRESSED> &baluba::logo(void) {
  return baluba_logo;
}

#ifdef LED_PIN
// colors taken from the logo
static const CRGB BALUBA_OLIVE = CRGB(0x666600);
static const CRGB BALUBA_KHAKI = CRGB(0xbbbb77);
static const CRGB BALUBA_GREEN = CRGB(0x77ff77);
static const CRGB BALUBA_CREAM = CRGB(0xffffbb);

// light running back and forth over the olive base
void baluba::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if (sub_cnt++ != 32) {
    return;
  }
  sub_cnt = 0;

  static char led = 0;
  char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
  for (char c = 0; c < NUM_LEDS; c++) {
    leds[c] = (c == il) ? BALUBA_GREEN : BALUBA_OLIVE;
  }
  led = (led + 1) % (2 * NUM_LEDS - 2);
}

void baluba::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[7] = {
    BALUBA_OLIVE, BALUBA_KHAKI, BALUBA_GREEN, BALUBA_CREAM,
    BALUBA_GREEN, BALUBA_KHAKI, BALUBA_OLIVE
  };
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
