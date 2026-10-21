#include "../../catalog.h"
#ifdef GAME_SHOLLOW

#include "shollow.h"
#include "../../emulation/input.h"
#include "shollow_dipswitches.h"
#include "shollow_logo.h"
#include "shollow_rom.h"
#include "shollow_snd_rom.h"
#include "shollow_tilemap.h"
#include "shollow_spritemap.h"
#include "shollow_ssio_prom.h"

// SSIO IP0 / IP1, active low
static const uint8_t SH_IP0_COIN = 0x01, SH_IP0_START = 0x04;
static const uint8_t SH_IP1_LEFT = 0x01, SH_IP1_RIGHT = 0x02, SH_IP1_SHIELD = 0x04,
                     SH_IP1_FIRE = 0x08;
// shield on the unused stick axis: START is a synthesized coin + start
// press on boards without a COIN pin and cannot be held
static const unsigned int SHIELD_KEYS = BUTTON_UP | BUTTON_DOWN | BUTTON_B;

shollow::shollow()
  : tron(shollow_rom_blob, shollow_snd_rom_blob, shollow_tilemap_blob, shollow_spritemap_blob,
         shollow_ssio_duty_gain) { }

const Logo &shollow::logo(void) {
  return shollow_logo;
}

uint8_t shollow::read_input(uint8_t n) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  switch (n) {
  case 0:
    if (keys & BUTTON_COIN) {
      v &= ~SH_IP0_COIN;
    }
    if (keys & BUTTON_START) {
      v &= ~SH_IP0_START;
    }
    return v;
  case 1:
    if (keys & BUTTON_LEFT) {
      v &= ~SH_IP1_LEFT;
    }
    if (keys & BUTTON_RIGHT) {
      v &= ~SH_IP1_RIGHT;
    }
    if (keys & SHIELD_KEYS) {
      v &= ~SH_IP1_SHIELD;
    }
    if (keys & BUTTON_FIRE) {
      v &= ~SH_IP1_FIRE;
    }
    return v;
  case 3:
    return SHOLLOW_IP3;
  default:
    return 0xff;
  }
}

#ifdef LED_PIN
// logo colors (logos/shollow.png): red outline
static const CRGB SHOLLOW_LED_RED(0xe03020), SHOLLOW_LED_DARK(0x401008);

void shollow::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      SHOLLOW_LED_DARK, SHOLLOW_LED_RED, SHOLLOW_LED_RED, SHOLLOW_LED_RED,
      SHOLLOW_LED_RED, SHOLLOW_LED_RED, SHOLLOW_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// flash on each new sound command (shots, hits, wave events)
void shollow::gameLeds(CRGB *leds) {
  if (led_flash) {
    fill_solid(leds, NUM_LEDS, SHOLLOW_LED_RED);
    return;
  }
  menuLeds(leds);
}
#endif

#endif
