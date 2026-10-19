#ifndef PITFALL2_H
#define PITFALL2_H

#include "../system1/system1.h"
#include "pitfall2_dipswitches.h"

// Pitfall II (Sega 1985), unencrypted set pitfall2u
class pitfall2 : public system1 {
public:
  pitfall2();

  signed char machineType() override { return MCH_PITFALL2; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return PITFALL2_SWA; }
  uint8_t dswB() override { return PITFALL2_SWB; }
};

#endif
