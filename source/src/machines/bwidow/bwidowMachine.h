#ifndef BWIDOWMACHINE_H
#define BWIDOWMACHINE_H

#include "../dvgMachine.h"
#include "bwidow_layout.h"
#include "../../emulation/avg.h"
#include "../../emulation/er2055.h"

// Shared base of the Atari Black Widow board family (MAME atari/bwidow.cpp:
// Space Duel, Gravitar, Black Widow): M6502 at 12.096 MHz / 8 with a
// 246 Hz IRQ, AVG with 3 bit color, two POKEYs with the DIP switches on
// ALLPOT, ER2055 EAROM. Subclasses own the memory map and the inputs.
class bwidowMachine : public dvgMachine {
public:
  void reset() override;

protected:
  // CPU addresses of vector RAM and ROM
  struct Map {
    uint16_t vram, vram_size, vrom, vrom_size;
  };

  bwidowMachine(uint8_t (*read)(m6502_t *, uint16_t),
                void (*write)(m6502_t *, uint16_t, uint8_t),
                const Map &map, const bwidow_layout::Screen &screen,
                uint8_t dsw0, uint8_t dsw1);

  uint8_t pokey_read(uint8_t n, uint8_t reg);
  void pokey_write(uint8_t n, uint8_t reg, uint8_t v);
  void earom_control(uint8_t v);
  void avg_reset();

  const unsigned char *vrom = nullptr; // set by the subclass ctor
  Er2055 earom;
  uint8_t flip = 0; // bwidow_layout::FLIP_X | FLIP_Y

private:
  static const uint8_t POKEYS = 2;

  bool pokey_running(uint8_t n) const;
  uint16_t decode_frame(const uint8_t *vram) override;
  uint16_t map_lines(uint16_t count) override;
  const uint16_t *strip_palette(uint8_t strip) const override { return pal; }

  const Map map;
  const bwidow_layout::Screen screen;
  const uint8_t dsw[POKEYS];
  avg::State avg_state; // emulation core only (decode_frame)
  uint8_t allpot[POKEYS] = {};
  uint32_t random_base[POKEYS] = {}; // CPU cycle of the SKCTL reset release
  uint16_t pal[bwidow_layout::PALETTE_SIZE];
};

#endif
