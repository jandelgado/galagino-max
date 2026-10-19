#ifndef ASTEROIDS_H
#define ASTEROIDS_H

#include "asteroids_logo.h"
#include "asteroids_dipswitches.h"
#include "../dvgMachine.h"
#include "asteroids_sound.h"

// Atari Asteroids, MAME asteroid (rev 4), atari/asteroid.cpp.
// One M6502 at 12.096 MHz / 8, DVG vector display 1024x768, discrete sound,
// ROT0. The picture stays upright, scaled to the panel width and centered
// vertically:
//
//   native (landscape)            panel (portrait, 240 x 288)
//   y 906 +-----------+           +-----------------+ row 0
//         |           |           +-----------------+ row 51  = y 906
//         |           |    ->     |    185 high     |
//   y 118 +-----------+           +-----------------+ row 235 = y 118
//         x 0      x 1023         +-----------------+ row 287
//                                 col 0 = x 0, col 239 = x 1023
class asteroids : public dvgMachine {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> asteroids_rom;
  Asset<unsigned char, COMPRESSED> asteroids_vrom;
public:
  asteroids();
  signed char machineType() override { return MCH_ASTEROIDS; }
  void renderAudio(int16_t *out, int n) override { sound.render(soundregs, out, n); }
  void reset() override;
  static const Logo &logo() { return asteroids_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t in0(uint8_t bit) const;
  uint8_t in1(uint8_t bit) const;
  bool in_game() const;
  uint16_t map_lines(uint16_t count) override;

  const unsigned char *rom = nullptr, *vrom = nullptr;
  bool ramsel = false;   // outlatch bit 2: swap player RAM pages 2 and 3
  uint8_t lamps = 0;     // outlatch bits 0-1: START lamps, active high here

private:
  AsteroidsSound sound;
};

#endif
