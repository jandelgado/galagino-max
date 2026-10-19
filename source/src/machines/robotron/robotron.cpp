#include "robotron.h"

// romMain/romSound unpack before hw's Arena::alloc calls (member order).
// The three allocs are unordered among themselves; every order fits.
robotron::robotron()
  : williams(hw, ROBOTRON_COLUMNS),
    robotron_rom_main(robotron_rom_main_blob), robotron_rom_sound(robotron_rom_sound_blob),
    romMain(robotron_rom_main.data()), romSound(robotron_rom_sound.data()),
    hw(Arena::alloc(WilliamsBoard::RAM_SIZE), Arena::alloc(Scanout::BUF_BYTES),
       Arena::alloc(Scanout::BUF_BYTES), romMain, romSound) {}

#ifdef LED_PIN
// colors from logos/robotron.png: red, yellow
void robotron::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = { CRGB(0xff0000), CRGB(0xffff00), CRGB(0xff0000),
      CRGB(0xffff00), CRGB(0xff0000), CRGB(0xffff00), CRGB(0xff0000) };
  memcpy(leds, menu_leds, sizeof(menu_leds));
}

// menu colors, scaled by the DAC swing: LEDs pulse with the sound
void robotron::gameLeds(CRGB *leds) {
  menuLeds(leds);
  const int swing = abs((int)hw.sound().dac() - 0x80);  // 0..128
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].nscale8(64 + swing);
  }
}
#endif
