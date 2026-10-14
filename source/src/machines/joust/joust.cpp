#include "joust.h"

// romMain/romSound unpack before hw's Arena::alloc calls (member order).
// The three allocs are unordered among themselves; every order fits.
joust::joust()
  : williams(hw, JOUST_COLUMNS),
    joust_rom_main(joust_rom_main_blob), joust_rom_sound(joust_rom_sound_blob),
    romMain(joust_rom_main.data()), romSound(joust_rom_sound.data()),
    hw(Arena::alloc(WilliamsBoard::RAM_SIZE), Arena::alloc(Scanout::BUF_BYTES),
       Arena::alloc(Scanout::BUF_BYTES), romMain, romSound) {}

#ifdef LED_PIN
// colors from logos/joust.png: yellow, dark red
void joust::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = { CRGB(0xffff00), CRGB(0xb90000), CRGB(0xffff00),
      CRGB(0xb90000), CRGB(0xffff00), CRGB(0xb90000), CRGB(0xffff00) };
  memcpy(leds, menu_leds, sizeof(menu_leds));
}

// menu colors, scaled by the DAC swing: LEDs pulse with the sound
void joust::gameLeds(CRGB *leds) {
  menuLeds(leds);
  const int swing = abs((int)hw.sound().dac() - 0x80);  // 0..128
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].nscale8(64 + swing);
  }
}
#endif
