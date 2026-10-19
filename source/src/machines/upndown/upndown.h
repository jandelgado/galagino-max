#ifndef UPNDOWN_H
#define UPNDOWN_H

#include "../system1/system1.h"
#include "upndown_arena_plan.h"
#include "upndown_dipswitches.h"

// Up'n Down (Sega 1983), unencrypted set upndownu
class upndown : public system1 {
public:
  upndown();

  signed char machineType() override { return MCH_UPNDOWN; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return UPNDOWN_SWA; }
  uint8_t dswB() override { return UPNDOWN_SWB; }
};

#endif
