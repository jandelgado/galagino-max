#ifndef MRVIKING_H
#define MRVIKING_H

#include "../system1/system1.h"
#include "mrviking_arena_plan.h"
#include "mrviking_dipswitches.h"

// Mister Viking (Sega 1984), 315-5041 encrypted main CPU
class mrviking : public system1 {
public:
  mrviking();

  uint8_t machineType() override { return MCH_MRVIKING; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return MRVIKING_SWA; }
  uint8_t dswB() override { return MRVIKING_SWB; }
};

#endif
