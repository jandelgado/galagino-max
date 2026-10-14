#ifndef ROBOTRON_H
#define ROBOTRON_H

#include "../williams/williams.h"
#include "../williams/robotron_board.h"
#include "robotron_rom.h"
#include "robotron_logo.h"

// Robotron: 2084 (Williams / Vid Kidz 1982), MAME williams/williams.cpp,
// set robotron.
class robotron : public williams {
  // declaration order = Arena order: ROM assets, then the board's RAM
  // and scanout allocations
  Asset<unsigned char, COMPRESSED> robotron_rom_main;
  Asset<unsigned char, COMPRESSED> robotron_rom_sound;
  const uint8_t *romMain, *romSound;
  RobotronBoard hw;

public:
  robotron();
  signed char machineType() override { return MCH_ROBOTRON; }
  static const Logo &logo() { return robotron_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
};

#endif
