#ifndef SXEVIOUS_H
#define SXEVIOUS_H

#include "../xevious/xevious.h"

// Super Xevious (Namco 1984): xevious board, own main and sub CPU ROMs.
// Every other asset is shared with xevious (MAME galaga.cpp, CRCs).
class sxevious : public xevious {
public:
  sxevious();

  signed char machineType() override { return MCH_SXEVIOUS; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override { menuLeds(leds); }
#endif
};

#endif
