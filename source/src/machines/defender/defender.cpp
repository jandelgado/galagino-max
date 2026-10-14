#include "defender.h"

// action key: START with a COIN pin or the galagino controller, else EXTRA
// (gng JUMP_KEY pattern)
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int ACTION_KEY = BUTTON_START;
#else
static const unsigned int ACTION_KEY = BUTTON_EXTRA;
#endif

// romMain..romSound unpack before hw's Arena::alloc calls (member order).
// The three allocs are unordered among themselves; every order fits.
defender::defender()
  : williams(hw, DEFENDER_COLUMNS),
    defender_rom_main(defender_rom_main_blob), defender_rom_banked(defender_rom_banked_blob),
    defender_rom_sound(defender_rom_sound_blob), williams_rgb(williams_rgb_blob),
    romMain(defender_rom_main.data()), romBanked(defender_rom_banked.data()),
    romSound(defender_rom_sound.data()),
    hw(Arena::alloc(WilliamsBoard::RAM_SIZE), Arena::alloc(Scanout::BUF_BYTES),
       Arena::alloc(Scanout::BUF_BYTES), romMain, romBanked, romSound, ACTION_KEY) {
  setPalette(williams_rgb.data());
}

#ifdef LED_PIN
// colors from logos/defender.png: red, yellow
void defender::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = { CRGB(0xff0000), CRGB(0xffff00), CRGB(0xff0000),
      CRGB(0xffff00), CRGB(0xff0000), CRGB(0xffff00), CRGB(0xff0000) };
  memcpy(leds, menu_leds, sizeof(menu_leds));
}

// white while a smart bomb fires, else the menu colors
void defender::gameLeds(CRGB *leds) {
  if (hw.smartBomb()) {
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB(0xffffff);
    }
    return;
  }
  menuLeds(leds);
}
#endif
