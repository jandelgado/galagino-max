#ifndef CHOPLIFT_H
#define CHOPLIFT_H

#include "../system1/system1.h"
#include "choplift_arena_plan.h"
#include "choplift_dipswitches.h"

// Choplifter (Sega 1985), unprotected set chopliftu on the System 2 board
class choplift : public system1 {
public:
  choplift();

  void reset() override;
  uint8_t machineType() override { return MCH_CHOPLIFT; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return CHOPLIFT_OPTIONS; }
  uint8_t dswB() override { return CHOPLIFT_COINAGE; }
};

#endif
