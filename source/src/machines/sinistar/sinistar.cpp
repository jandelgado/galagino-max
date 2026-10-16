#include "sinistar.h"

static_assert(0x0800 + 0x1000 <= RAMSIZE, "Sinistar SRAM in memory[]");

// action key (sinibomb): START with a COIN pin or the galagino
// controller, else EXTRA (defender.cpp)
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int ACTION_KEY = BUTTON_START;
#else
static const unsigned int ACTION_KEY = BUTTON_EXTRA;
#endif

// main ROM unpacks before hw's Arena::alloc calls (member order); every
// order of the three board allocs fits (plan: Arena budget)
sinistar::sinistar()
  : williams(hw),
    sinistar_rom_main(sinistar_rom_main_blob), sinistar_rom_sound(sinistar_rom_sound_blob),
    romMain(sinistar_rom_main.data()), romSound(sinistar_rom_sound.data()),
    hw(Arena::alloc(WilliamsBoard::RAM_SIZE), Arena::alloc(Scanout::BUF_BYTES),
       Arena::alloc(Scanout::BUF_BYTES), romMain, romSound, ACTION_KEY) {}

// SRAM binds after machineBase::reset() cleared memory[], before the CPU runs
void sinistar::reset() {
  williams::reset();
  hw.bindSram(memory + SRAM_OFS);
}

#ifdef LED_PIN
// logos/sinistar.png has one color (red 0xf80000): symmetric gradient,
// brightest in the middle
void sinistar::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = { CRGB(0x3e0000), CRGB(0x7c0000), CRGB(0xba0000),
      CRGB(0xf80000), CRGB(0xba0000), CRGB(0x7c0000), CRGB(0x3e0000) };
  memcpy(leds, menu_leds, sizeof(menu_leds));
}

// menu colors, scaled by the DAC swing: LEDs pulse with the sound
void sinistar::gameLeds(CRGB *leds) {
  menuLeds(leds);
  const int swing = abs((int)hw.sound().dac() - 0x80);  // 0..128
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].nscale8(64 + swing);
  }
}
#endif
