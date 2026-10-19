#ifndef JOUST_BOARD_H
#define JOUST_BOARD_H

#include "b1_board.h"

// Joust: IN0 b0-3 = LS157 mux of INP2 (select 0) / INP1 (select 1),
// select = PIA0 CB2. Player 1 only: left, right, flap on FIRE.
class JoustBoard : public B1Board {
public:
  JoustBoard(uint8_t *ram, uint8_t *scanA, uint8_t *scanB, const uint8_t *rom,
             const uint8_t *soundRom)
    : B1Board(config(), ram, scanA, scanB, rom, soundRom) {}

  uint8_t in0ForTest(bool select) { return in0Mux(select); }

protected:
  uint8_t in0() override { return in0Mux(piaCb2(0)); }
  uint8_t in1() override { return 0; }

private:
  static const uint8_t LEFT = 0x01, RIGHT = 0x02, FLAP = 0x04, START1 = 0x20;
  static const uint16_t ADVANCE_FRAME = 750;

  // IN2 b0 = 0 at boot (MAME probe); Advance after the RAM test
  static const BoardConfig &config() {
    static const BoardConfig cfg = { 3, 0, 0x3ff, 0xf000, ADVANCE_FRAME };
    return cfg;
  }

  uint8_t in0Mux(bool selectP1) {
    const unsigned int k = keys();
    uint8_t v = (k & BOARD_KEY_START) ? START1 : 0;
    if (!selectP1) {
      return v;
    }
    if (k & BOARD_KEY_LEFT) {
      v |= LEFT;
    }
    if (k & BOARD_KEY_RIGHT) {
      v |= RIGHT;
    }
    if (k & BOARD_KEY_FIRE) {
      v |= FLAP;
    }
    return v;
  }
};

#endif
