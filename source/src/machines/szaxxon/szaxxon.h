#ifndef SZAXXON_H
#define SZAXXON_H

#include "../zaxxon/zaxxon.h"
#include "szaxxon_arena_plan.h"

// Super Zaxxon (Sega 1982): zaxxon board, all ROMs its own, main CPU
// encrypted (315-5013). Samples are zaxxon's (MAME sampleof="zaxxon").
class szaxxon : public zaxxon {
public:
  szaxxon();

  uint8_t machineType() override { return MCH_SZAXXON; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
#endif
};

#endif
