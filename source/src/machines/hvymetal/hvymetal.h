#ifndef HVYMETAL_H
#define HVYMETAL_H

#include "../system1/system1.h"
#include "hvymetal_arena_plan.h"
#include "hvymetal_dipswitches.h"

// Heavy Metal (Sega 1985), 315-5135 encrypted main CPU, banked main ROM
class hvymetal : public system1 {
public:
  hvymetal();

  void reset() override;
  signed char machineType() override { return MCH_HVYMETAL; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return HVYMETAL_SWA; }
  uint8_t dswB() override { return HVYMETAL_SWB; }
};

#endif
