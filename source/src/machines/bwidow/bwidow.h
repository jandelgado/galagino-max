#ifndef BWIDOW_H
#define BWIDOW_H

#include "bwidowMachine.h"
#include "bwidow_logo.h"
#include "bwidow_dipswitches.h"
#include "bwidow_game_layout.h"
#include "bwidow_inputs.h"

// Atari Black Widow, MAME bwidow, atari/bwidow.cpp. Black Widow board
// (bwidowMachine), ROT0, panel layout in bwidow_game_layout.h. Player 1
// only.
//
// Controls (twin sticks on one stick, bwidow_inputs.h): stick moves, FIRE
// fires where the stick points and keeps that aim while held, X/B/A/Y
// fire up/right/down/left, START = coin + start.
class bwidow : public bwidowMachine {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> bwidow_rom;
  Asset<unsigned char, COMPRESSED> bwidow_vrom;
public:
  bwidow();
  signed char machineType() override { return MCH_BWIDOW; }
  void reset() override;
  static const Logo &logo() { return bwidow_logo; }
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
  RobotronSticks sticks;
  uint16_t stick_state = 0; // last sticks.update(), read by IN3, IN4
};

#endif
