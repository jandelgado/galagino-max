#ifndef ROBOTRON_BOARD_H
#define ROBOTRON_BOARD_H

#include "b1_board.h"
#include "../../emulation/twin_stick.h"

// Robotron: 2084, twin sticks mapped to one stick + FIRE (strafe lock)
class RobotronBoard : public B1Board {
public:
  RobotronBoard(uint8_t *ram, uint8_t *scanA, uint8_t *scanB, const uint8_t *rom,
                const uint8_t *soundRom)
    : B1Board(config(), ram, scanA, scanB, rom, soundRom) {}

protected:
  void frameInputs() override {
    inputs = sticks.update(keys());
    if (keys() & BOARD_KEY_START) {
      inputs |= START1;
    }
  }
  uint8_t in0() override { return inputs & 0xff; }
  uint8_t in1() override { return inputs >> 8; }

private:
  static const uint16_t START1 = 0x10;  // IN0 b4

  // IN2 b0 = 0 at boot (MAME probe); Advance after the RAM test (host
  // boot test reaches attract)
  static const uint16_t ADVANCE_FRAME = 750;

  static const BoardConfig &config() {
    static const BoardConfig cfg = { 3, 0, 0x3ff, 0xf000, ADVANCE_FRAME };
    return cfg;
  }

  TwinStick sticks;
  uint16_t inputs = 0;
};

#endif
