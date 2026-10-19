#ifndef MOTOS_H
#define MOTOS_H

#include "../digdug2/digdug2.h"

// Motos (Namco 1985), MAME mappy.cpp motos(): Dig Dug II board with a 56XX
// instead of the 58XX as I/O chip 0, own ROMs. DIP switch values match
// digdug2's (MAME defaults: 3 lives, 1C1C, rank A, demo sounds on).
// FIRE = button 1 (power), START or B = button 2 (jump).
class motos : public digdug2 {
public:
  motos();

  signed char machineType() override { return MCH_MOTOS; }
  static const Logo &logo(void);
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
};

#endif
