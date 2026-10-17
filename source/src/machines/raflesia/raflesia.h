#ifndef RAFLESIA_H
#define RAFLESIA_H

#include "../system1/system1.h"
#include "raflesia_arena_plan.h"
#include "raflesia_dipswitches.h"

// Rafflesia (Coreland / Sega 1986), unencrypted set raflesiau
class raflesia : public system1 {
public:
  raflesia();

  signed char machineType() override { return MCH_RAFLESIA; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return RAFLESIA_SWA; }
  uint8_t dswB() override { return RAFLESIA_SWB; }
};

#endif
