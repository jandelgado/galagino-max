#ifndef GARDIA_H
#define GARDIA_H

#include "../system1/system1.h"
#include "gardia_dipswitches.h"

// Gardia (Coreland / Sega 1986), 317-0006 encrypted main CPU, banked main ROM
class gardia : public system1 {
public:
  gardia();

  signed char machineType() override { return MCH_GARDIA; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return GARDIA_SWA; }
  uint8_t dswB() override { return GARDIA_SWB; }
};

#endif
