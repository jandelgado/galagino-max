#ifndef JOUST_H
#define JOUST_H

#include "../williams/williams.h"
#include "../williams/joust_board.h"
#include "joust_rom.h"
#include "joust_logo.h"

// Joust (Williams 1982), MAME williams/williams.cpp, set joust.
class joust : public williams {
  // declaration order = Arena order: ROM assets, then the board's RAM
  // and scanout allocations
  Asset<unsigned char, COMPRESSED> joust_rom_main;
  Asset<unsigned char, COMPRESSED> joust_rom_sound;
  const uint8_t *romMain, *romSound;
  JoustBoard hw;

public:
  joust();
  signed char machineType() override { return MCH_JOUST; }
  static const Logo &logo() { return joust_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
};

#endif
