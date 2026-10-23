#ifndef LLANDER_H
#define LLANDER_H

#include "llander_logo.h"
#include "llander_dipswitches.h"
#include "../dvgMachine.h"
#include "llander_sound.h"

// Atari Lunar Lander, MAME llander (rev 2), atari/asteroid.cpp.
// Asteroids hardware with 8 KiB program ROM, 6 KiB vector ROM, an analog
// thrust lever and discrete sound. Upright like asteroids; text is
// enlarged by llander_layout (HUD in the borders).
//
// Controls: stick left/right rotate, up/down move the thrust lever (it
// stays where left, like the real one), FIRE = Select Game (before
// start) and Abort (in game), START = coin + start.
class llander : public dvgMachine {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> llander_rom;
  Asset<unsigned char, COMPRESSED> llander_vrom;
public:
  llander();
  uint8_t machineType() override { return MCH_LLANDER; }
  void renderAudio(int16_t *out, int n) override { sound.render(soundregs, out, n); }
  void reset() override;
  static const Logo &logo() { return llander_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t in1(uint8_t bit) const;
  bool in_game() const;
  void frame_input() override;
  uint16_t decode_frame(const uint8_t *vram) override;
  uint16_t map_lines(uint16_t count) override;
  const uint8_t *mission_list();

  const unsigned char *rom = nullptr, *vrom = nullptr;
  uint8_t thrust = 0; // lever 0..THRUST_MAX, read at 0x2c00
  uint8_t lamps = 0;  // outlatch: mission lamps, start LED
  uint8_t mission[2 * 11]; // DVG list: LABS, up to 8 glyphs, HALT

private:
  LlanderSound sound;
};

#endif
