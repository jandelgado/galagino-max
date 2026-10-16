#ifndef SINISTAR_BOARD_H
#define SINISTAR_BOARD_H

#include "b1_board.h"

// Sinistar: 49-way stick (IN0, full deflection from the digital stick),
// IN1 fire, sinibomb (action key), start. Speech board on the sound PIA.
class SinistarBoard : public B1Board {
public:
  SinistarBoard(uint8_t *ram, uint8_t *scanA, uint8_t *scanB, const uint8_t *rom,
                const uint8_t *soundRom, unsigned int actionKey)
    : B1Board(config(), ram, scanA, scanB, rom, soundRom, B1_SINISTAR), actionKey(actionKey) {}

  // MAME translate49 at x/y = 0x00, 0x38, 0x6f: 0x0, 0x7, 0x8; Y reversed
  static uint8_t stick49(unsigned int k) {
    uint8_t x = CENTER, y = CENTER;
    if (k & BOARD_KEY_LEFT) {
      x = LOW;
    } else if (k & BOARD_KEY_RIGHT) {
      x = HIGH;
    }
    if (k & BOARD_KEY_UP) {
      y = HIGH;
    } else if (k & BOARD_KEY_DOWN) {
      y = LOW;
    }
    return (uint8_t)(x << 4 | y);
  }

protected:
  void frameInputs() override {
    const unsigned int k = keys();
    stick = stick49(k);
    buttons = 0;
    if (k & BOARD_KEY_FIRE) {
      buttons |= FIRE;
    }
    if (k & (actionKey | BOARD_KEY_A)) {
      buttons |= BOMB;
    }
    if (k & BOARD_KEY_START) {
      buttons |= START1;
    }
  }
  uint8_t in0() override { return stick; }
  uint8_t in1() override { return buttons; }

private:
  static const uint8_t LOW = 0x0, CENTER = 0x7, HIGH = 0x8;
  static const uint8_t FIRE = 0x01, BOMB = 0x02, START1 = 0x10;  // IN1
  static const uint16_t ADVANCE_FRAME = 750;  // as Robotron; Step 5 checks it

  static const BoardConfig &config() {
    static const BoardConfig cfg = { 3, 0, 0x3ff, 0xb000, ADVANCE_FRAME, 1 };
    return cfg;
  }

  unsigned int actionKey;
  uint8_t stick = 0x77, buttons = 0;
};

#endif
