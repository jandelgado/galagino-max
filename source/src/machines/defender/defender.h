#ifndef DEFENDER_H
#define DEFENDER_H

#include "../williams/williams.h"
#include "../williams/defender_board.h"
#include "../williams/williams_palette.h"
#include "defender_rom.h"
#include "defender_logo.h"

// Defender (Williams 1980), MAME williams/williams.cpp, set defender.
class defender : public williams {
  // declaration order = Arena order: ROM assets, then the board's RAM
  // and scanout allocations
  Asset<unsigned char, COMPRESSED> defender_rom_main;
  Asset<unsigned char, COMPRESSED> defender_rom_banked;
  Asset<unsigned char, COMPRESSED> defender_rom_sound;
  Asset<unsigned short, COMPRESSED> williams_rgb;
  const uint8_t *romMain, *romBanked, *romSound;
  DefenderBoard hw;

public:
  defender();
  signed char machineType() override { return MCH_DEFENDER; }
  static const Logo &logo() { return defender_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
};

#endif
