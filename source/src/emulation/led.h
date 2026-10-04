#ifndef LED_H
#define LED_H

#include "config.h"
#include "../machines/machineBase.h"

#ifdef LED_PIN
#include <FastLED.h>

class Led {
public:
    void init(void);
    void update(const machineInfo *machines, machineBase *currentMachine, signed char machineIndexPreselection, signed char machineSelected);
    void fill(CRGB color);

private:
    void show(void);

    CRGB leds[NUM_LEDS];
    CRGB ledsBackup[NUM_LEDS];
 
};
  
#endif // LED_PIN 

#endif // LED_H