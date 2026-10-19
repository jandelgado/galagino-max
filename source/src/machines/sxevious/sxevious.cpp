#ifdef ENABLE_SXEVIOUS

#include "sxevious.h"
#include "sxevious_logo.h"
#include "sxevious_rom_cpu1.h"
#include "sxevious_rom_cpu2.h"

// DSW B: xevious factory default except bit 7. sxevious inverts Freeze
// (MAME PORT_DIPNAME 0x80, 0x00, "Freeze"): 0xff would freeze the game.
static constexpr uint8_t SXEVIOUS_DSWB = 0x7f;

sxevious::sxevious() : xevious(sxevious_rom_cpu1_blob, sxevious_rom_cpu2_blob, SXEVIOUS_DSWB) { }

const Logo &sxevious::logo(void) {
  return sxevious_logo;
}

#ifdef LED_PIN
// from logos/sxevious.png: cyan and blue letters, red shade
static const CRGB SXEVIOUS_LED_A = CRGB(0x33c5e4);
static const CRGB SXEVIOUS_LED_B = CRGB(0x187ff7);
static const CRGB SXEVIOUS_LED_C = CRGB(0x9d010e);

void sxevious::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      SXEVIOUS_LED_C, SXEVIOUS_LED_B, SXEVIOUS_LED_A, SXEVIOUS_LED_A,
      SXEVIOUS_LED_A, SXEVIOUS_LED_B, SXEVIOUS_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif

#endif // ENABLE_SXEVIOUS
