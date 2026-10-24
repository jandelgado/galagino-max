#ifndef JUNGLEH_H
#define JUNGLEH_H

#include "../elevator/elevator.h"

// Jungle Hunt (Taito America 1982), MAME taito/taitosj.cpp, set jungleh:
// same nomcu board and init as the elevatorb bootleg, own ROMs, DIPs and
// idle loops. ROT180 screen, the game sets flip x+y: upright as drawn
// (elevator.cpp render_row). 8-way stick, FIRE jumps and stabs.
class jungleh : public elevator {
public:
  jungleh();

  uint8_t machineType() override { return MCH_JUNGLEH; }
  static const Logo &logo(void);
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
};

#endif
