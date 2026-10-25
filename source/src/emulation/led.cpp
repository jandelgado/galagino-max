#include "led.h"
#include "../hal/hal.h"

#ifdef LED_PIN
void Led::init() {
  hal::LedStrip::init(LED_PIN, NUM_LEDS);
}

void Led::update(machineBase *machine) {
  machine->gameLeds(leds);
  show();
}

void Led::fill(CRGB color) {
  fill_solid(leds, NUM_LEDS, color);
  show();
}

// push leds to the strip, only if changed
void Led::show() {
  bool changed = false;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledsBackup[i].raw[0] != leds[i].raw[0] ||
        ledsBackup[i].raw[1] != leds[i].raw[1] ||
        ledsBackup[i].raw[2] != leds[i].raw[2]) {
      changed = true;
      break;
    }
  }

  if (!changed)
    return;
    
  for (int i = 0; i < NUM_LEDS; i++) {
    ledsBackup[i] = leds[i];
    //printf("%03d%03d%03d;", leds[i].raw[0], leds[i].raw[1], leds[i].raw[2]);
  }
  //printf("\n");
  hal::LedStrip::show(leds[0].raw, NUM_LEDS);  // leds is contiguous packed r,g,b
}
#endif
