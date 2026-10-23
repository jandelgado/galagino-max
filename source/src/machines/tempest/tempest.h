#ifndef TEMPEST_H
#define TEMPEST_H

#include "../dvgMachine.h"
#include "../../emulation/pokey.h"
#include "tempest_logo.h"
#include "tempest_dipswitches.h"
#include "tempest_layout.h"
#include "../../emulation/spinner.h"
#include "../../emulation/avg.h"
#include "../../emulation/er2055.h"
#include "../../emulation/mathbox.h"

// Atari Tempest, MAME tempest (rev 3, revised hardware), atari/tempest.cpp.
// Battle Zone board family: M6502 at 12.096 MHz / 8 with a 246 Hz IRQ, AVG
// (Tempest variant: 16 entry color RAM), Mathbox, two POKEYs, ER2055
// EAROM. ROT270, panel layout in tempest_layout.h.
//
// The AVG never halts: its list loops back to address 0 while the CPU
// rewrites the sublists. So the list is decoded once per emulated frame,
// at the end of run_frame(), not at GO.
//
// Controls: stick left/right turns the spinner (emulation/spinner.h), FIRE
// fires, up/down superzaps, START = coin + start.
class tempest : public dvgMachine {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> tempest_rom;
  Asset<unsigned char, COMPRESSED> tempest_vrom;
public:
  tempest();
  uint8_t machineType() override { return MCH_TEMPEST; }
  void renderAudio(int16_t *out, int n) override {
    Pokey::renderBank(pokey, soundregs, out, n);
    audioClampAll(out, n, AUDIO_MAX);
  }
  void reset() override;
  void run_frame() override;
  static const Logo &logo() { return tempest_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const uint8_t POKEYS = 2;
  // placement in dvgMachine::memory: VRAM (4 KB) at VRAM_OFS, color RAM,
  // program RAM (2 KB)
  static const uint16_t COLOR_OFS = VRAM_OFS + 0x1000, COLOR_SIZE = 0x10;
  static const uint16_t RAM_OFS = COLOR_OFS + COLOR_SIZE;

  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t in0() const;
  uint8_t in1() const;
  uint8_t in2() const;
  bool avg_halted() const;
  bool pokey_running(uint8_t n) const;
  uint8_t pokey_read(uint8_t n, uint8_t reg);
  void pokey_write(uint8_t n, uint8_t reg, uint8_t v);
  void frame_input() override;
  uint16_t decode_frame(const uint8_t *vram) override;
  uint16_t map_lines(uint16_t count) override;
  const uint16_t *strip_palette(uint8_t strip) const override { return pal; }

  const unsigned char *rom = nullptr, *vrom = nullptr;
  Mathbox mathbox;
  Er2055 earom;
  // 72 counts per knob turn; at most 7 counts per frame (signed 4 bit
  // difference): 0.5 .. 6 counts per frame, in quarter counts
  static const uint8_t SPIN_MIN = 2, SPIN_MAX = 24, SPIN_ACCEL = 1;
  Spinner spinner{SPIN_MIN, SPIN_MAX, SPIN_ACCEL};
  avg::State avg_state; // emulation core only (decode_frame)
  bool avg_running = false; // GO seen since the last AVG reset
  uint8_t flip = 0;         // tempest_layout::FLIP_X | FLIP_Y
  uint8_t lamps = 0xff;     // 0x60e0, start lamps active low
  uint8_t allpot[POKEYS] = {};
  uint32_t random_base[POKEYS] = {}; // CPU cycle of the SKCTL reset release
  uint16_t pal[tempest_layout::PALETTE_SIZE];

private:
  Pokey pokey[2] = {Pokey(Pokey::CLOCKS_1512KHZ), Pokey(Pokey::CLOCKS_1512KHZ)};
};

#endif
