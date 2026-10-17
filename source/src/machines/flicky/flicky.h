#ifndef FLICKY_H
#define FLICKY_H

#include "../system1/system1.h"
#include "flicky_arena_plan.h"
#include "flicky_dipswitches.h"

// Flicky (Sega 1984), unencrypted 128k set flickys2
class flicky : public system1 {
public:
  flicky();

  signed char machineType() override { return MCH_FLICKY; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return FLICKY_SWA; }
  uint8_t dswB() override { return FLICKY_SWB; }
};

#endif
