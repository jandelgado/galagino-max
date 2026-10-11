#ifndef SPACDUEL_H
#define SPACDUEL_H

#include "../dvgMachine.h"
#include "spacduel_logo.h"
#include "spacduel_dipswitches.h"
#include "spacduel_layout.h"
#include "../../emulation/avg.h"
#include "../../emulation/er2055.h"

// Atari Space Duel, MAME spacduel (version 2), atari/bwidow.cpp.
// Black Widow board family: M6502 at 12.096 MHz / 8 with a 246 Hz IRQ,
// AVG with 3 bit color, two POKEYs, ER2055 EAROM. ROT0, upright, panel
// layout in spacduel_layout.h. Player 1 only.
//
// Controls: stick left/right rotates, up thrusts, down (or B) shields,
// FIRE (or A) fires, START = coin + start.
class spacduel : public dvgMachine {
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
  static const uint8_t POKEYS = 2;

  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t in0() const;
  uint8_t in1(uint8_t offset) const;
  bool pokey_running(uint8_t n) const;
  uint8_t pokey_read(uint8_t n, uint8_t reg);
  void pokey_write(uint8_t n, uint8_t reg, uint8_t v);
  uint16_t decode_frame(const uint8_t *vram) override;
  uint16_t map_lines(uint16_t count) override;
  const uint16_t *strip_palette(uint8_t strip) const override { return pal; }

  const unsigned char *rom = nullptr, *vrom = nullptr;
  Er2055 earom;
  avg::State avg_state;     // emulation core only (decode_frame)
  uint8_t out = 0xff;       // 0x0c00: lamps active low, flip bits inverted
  uint8_t allpot[POKEYS] = {};
  uint32_t random_base[POKEYS] = {}; // CPU cycle of the SKCTL reset release
  uint16_t pal[spacduel_layout::PALETTE_SIZE];
};

#endif
