#ifndef SPACDUEL_H
#define SPACDUEL_H

#include "../bwidow/bwidowMachine.h"
#include "spacduel_logo.h"
#include "spacduel_dipswitches.h"
#include "spacduel_layout.h"
#include "spacduel_inputs.h" // after dvgMachine.h: needs input.h

// Atari Space Duel, MAME spacduel (version 2), atari/bwidow.cpp.
// Black Widow board family: M6502 at 12.096 MHz / 8 with a 246 Hz IRQ,
// AVG with 3 bit color, two POKEYs, ER2055 EAROM. ROT0, upright, panel
// layout in spacduel_layout.h. Player 1 only.
//
// Controls: stick left/right rotates, up thrusts, down (or B) shields,
// FIRE (or A) fires, START = coin + start.
class spacduel : public bwidowMachine {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> spacduel_rom;
  Asset<unsigned char, COMPRESSED> spacduel_vrom;
public:
  spacduel();
  signed char machineType() override { return MCH_SPACDUEL; }
  void reset() override;
  static const Logo &logo() { return spacduel_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t in0() const;
  uint8_t in1(uint8_t offset) const;

  const unsigned char *rom = nullptr;
  uint8_t out = 0xff; // 0x0c00: lamps active low, flip bits inverted
};

#endif
