#ifndef GRAVITAR_H
#define GRAVITAR_H

#include "../bwidow/bwidowMachine.h"
#include "gravitar_logo.h"
#include "gravitar_dipswitches.h"
#include "gravitar_layout.h"
#include "gravitar_inputs.h" // after dvgMachine.h: needs input.h

// Atari Gravitar, MAME gravitar (version 3), atari/bwidow.cpp. Black Widow
// board (bwidowMachine), ROT0, panel layout in gravitar_layout.h. Player 1
// only.
//
// Controls: stick left/right rotates, up thrusts, down (or B) shield and
// tractor beam, FIRE (or A) fires, START = coin + start.
class gravitar : public bwidowMachine {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> gravitar_rom;
  Asset<unsigned char, COMPRESSED> gravitar_vrom;
public:
  gravitar();
  signed char machineType() override { return MCH_GRAVITAR; }
  void reset() override;
  static const Logo &logo() { return gravitar_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  // RAM 0x0000-0x03ff at memory[0], 0x0400-0x07ff behind VRAM, which
  // dvgMachine keeps at VRAM_OFS (0x0400)
  static const uint16_t RAM_HIGH_OFS = VRAM_OFS + MAX_VRAM_SIZE;

  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t in0() const;
  uint8_t *ram(uint16_t a);

  const unsigned char *rom = nullptr;
  uint8_t misc = 0xff; // 0x8800 write: lamps active low
};

#endif
