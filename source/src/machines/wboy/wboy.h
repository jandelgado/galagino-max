#ifndef WBOY_H
#define WBOY_H

#include "../system1/system1.h"
#include "wboy_dipswitches.h"

// Wonder Boy (Escape / Sega 1986), unencrypted set wboy2u
class wboy : public system1 {
public:
  wboy();

  signed char machineType() override { return MCH_WBOY; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return WBOY_SWA; }
  uint8_t dswB() override { return WBOY_SWB; }
};

#endif
