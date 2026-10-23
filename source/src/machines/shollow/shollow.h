#ifndef SHOLLOW_H
#define SHOLLOW_H

#include "../tron/tron.h"

// Satan's Hollow (Bally Midway 1981), MAME midway/mcr.cpp: same machine
// config (mcr_90010), init and ROT90 as Tron, own ROMs and controls.
// 2-way stick, FIRE shoots, UP, DOWN or B raises the shield.
class shollow : public tron {
public:
  shollow();

  uint8_t machineType() override { return MCH_SHOLLOW; }
  static const Logo &logo(void);
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t read_input(uint8_t n) override;
  void update_controls() override { }
};

#endif
