#ifndef REGULUS_H
#define REGULUS_H

#include "../system1/system1.h"
#include "regulus_arena_plan.h"
#include "regulus_dipswitches.h"

// Regulus (Sega 1983), unencrypted set regulusu
class regulus : public system1 {
public:
  regulus();

  signed char machineType() override { return MCH_REGULUS; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return REGULUS_SWA; }
  uint8_t dswB() override { return REGULUS_SWB; }
};

#endif
