#ifndef BALUBA_H
#define BALUBA_H

#include "../starforce/starforce.h"
#include "baluba_arena_plan.h"

// Baluba-louk no Densetsu: Star Force board, own ROMs and DIP switches
class baluba : public starforce
{
public:
  baluba();

  signed char machineType() override { return MCH_BALUBA; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dsw1() override;
  uint8_t dsw2() override;
};

#endif
