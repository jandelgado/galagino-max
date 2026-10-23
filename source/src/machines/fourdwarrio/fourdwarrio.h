#ifndef FOURDWARRIO_H
#define FOURDWARRIO_H

#include "../system1/system1.h"
#include "fourdwarrio_arena_plan.h"
#include "fourdwarrio_dipswitches.h"

// 4-D Warriors (Coreland / Sega 1985), 315-5162 encrypted main CPU
class fourdwarrio : public system1 {
public:
  fourdwarrio();

  uint8_t machineType() override { return MCH_FOURDWARRIO; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return FOURDWARRIO_SWA; }
  uint8_t dswB() override { return FOURDWARRIO_SWB; }
};

#endif
