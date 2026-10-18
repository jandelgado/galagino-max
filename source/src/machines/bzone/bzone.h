#ifndef BZONE_H
#define BZONE_H

#include "../dvgMachine.h"
#include "bzone_logo.h"
#include "bzone_dipswitches.h"
#include "bzone_treads.h" // after dvgMachine.h: needs input.h
#include "bzone_layout.h"
#include "../../emulation/avg.h"
#include "../../emulation/mathbox.h"

// Atari Battle Zone, MAME bzone (rev 2), atari/bzone.cpp.
// Asteroids CPU board (M6502 at 12.096 MHz / 8, 246 Hz NMI) with an AVG
// vector generator, Mathbox, POKEY and discrete sound. ROT0, upright,
// panel layout in bzone_layout.h. The cabinet overlay is red above
// native y 80 (strips 0-10), green below.
//
// Controls: stick drives both treads (see bzone_treads.h), FIRE fires,
// START = coin + start.
class bzone : public dvgMachine {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> bzone_rom;
  Asset<unsigned char, COMPRESSED> bzone_vrom;
public:
  bzone();
  signed char machineType() override { return MCH_BZONE; }
  void reset() override;
  static const Logo &logo() { return bzone_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t in0() const;
  uint8_t in3() const;
  bool pokey_running() const;
  uint8_t pokey_read(uint8_t reg);
  void pokey_write(uint8_t reg, uint8_t v);
  uint16_t decode_frame(const uint8_t *vram) override;
  uint16_t map_lines(uint16_t count) override;
  const uint16_t *strip_palette(uint8_t strip) const override;

  const unsigned char *rom = nullptr, *vrom = nullptr;
  Mathbox mathbox;
  avg::State avg_state; // emulation core only (decode_frame)
  uint8_t allpot = 0;   // ALLPOT as of the last read while running
  uint32_t random_base = 0; // CPU cycle of the SKCTL reset release
  uint16_t red[16];     // palette for strips above the overlay boundary
};

#endif
