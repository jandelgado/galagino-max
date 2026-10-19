#ifndef IMSORRY_H
#define IMSORRY_H

#include "../system1/system1.h"
#include "imsorry_arena_plan.h"
#include "imsorry_dipswitches.h"

// I'm Sorry (Coreland / Sega 1985), 315-5110 encrypted main CPU
class imsorry : public system1 {
public:
  imsorry();

  signed char machineType() override { return MCH_IMSORRY; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return IMSORRY_SWA; }
  uint8_t dswB() override { return IMSORRY_SWB; }
};

#endif
