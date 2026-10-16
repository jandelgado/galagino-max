#ifndef SINISTAR_H
#define SINISTAR_H

#include "../williams/williams.h"
#include "../williams/sinistar_board.h"
#include "sinistar_rom.h"
#include "sinistar_logo.h"

// Sinistar (Williams 1982), MAME williams/williams.cpp, set sinistar
// (rev. 3 upright). ROT270: shown upright, 292 native columns -> 288 rows.
class sinistar : public williams {
  Asset<unsigned char, COMPRESSED> sinistar_rom_main;
  Asset<unsigned char, PLAIN> sinistar_rom_sound;
  const uint8_t *romMain, *romSound;
  SinistarBoard hw;

public:
  sinistar();
  void reset() override;
  signed char machineType() override { return MCH_SINISTAR; }
  static const Logo &logo() { return sinistar_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const uint16_t SRAM_OFS = 0x0800;  // memory[]: 0xd000-0xdfff
};

#endif
