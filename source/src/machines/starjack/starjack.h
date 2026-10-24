#ifndef STARJACK_H
#define STARJACK_H

#include "../system1/system1.h"
#include "starjack_arena_plan.h"
#include "starjack_dipswitches.h"

// Star Jacker (Sega 1983), the first Sega System 1 game
class starjack : public system1 {
public:
  starjack();

  uint8_t machineType() override { return MCH_STARJACK; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return STARJACK_SWA; }
  uint8_t dswB() override { return STARJACK_SWB; }
};

#endif
