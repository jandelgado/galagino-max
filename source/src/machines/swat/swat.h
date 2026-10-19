#ifndef SWAT_H
#define SWAT_H

#include "../system1/system1.h"
#include "swat_arena_plan.h"
#include "swat_dipswitches.h"

// SWAT (Coreland / Sega 1984), 315-5048 encrypted main CPU
class swat : public system1 {
public:
  swat();

  signed char machineType() override { return MCH_SWAT; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return SWAT_SWA; }
  uint8_t dswB() override { return SWAT_SWB; }
};

#endif
