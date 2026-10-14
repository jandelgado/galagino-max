#ifndef DEFENDER_CONTROLS_H
#define DEFENDER_CONTROLS_H

#include <stdint.h>
#include "williams_keys.h"

// Defender's 5 buttons + 2-way stick on stick + FIRE + one action key.
// Called once per frame; returns IN0 | IN1 << 8 (active high).
//   up/down: altitude; left/right toward the facing: thrust, against it:
//   reverse; action key: smart bomb, with left/right held: hyperspace.
class DefenderControls {
public:
  static const uint8_t FIRE = 0x01, THRUST = 0x02, SMART_BOMB = 0x04, HYPERSPACE = 0x08;
  static const uint8_t START1 = 0x20, REVERSE = 0x40, DOWN = 0x80;  // IN0
  static const uint8_t UP = 0x01;                                    // IN1
  static const int PULSE_FRAMES = 4;

  // ship direction in RAM: +3 facing right, -3 facing left (MAME probe
  // 2026-10-14: toggles with each Reverse press, constant otherwise)
  static const uint16_t RAM_DIRECTION = 0xa0bb;
  static bool facingLeft(uint8_t direction) { return direction & 0x80; }

  bool bombActive() const { return pulse > 0 && pulseBits == SMART_BOMB; }

  uint16_t update(unsigned int keys, bool facesLeft, unsigned int actionKey) {
    uint8_t in0 = 0, in1 = 0;
    if (keys & BOARD_KEY_FIRE) {
      in0 |= FIRE;
    }
    if (keys & BOARD_KEY_DOWN) {
      in0 |= DOWN;
    }
    if (keys & BOARD_KEY_UP) {
      in1 |= UP;
    }

    // thrust toward the stick, reverse first if the ship faces away
    const bool left = keys & BOARD_KEY_LEFT, right = keys & BOARD_KEY_RIGHT;
    if (left != right) {
      in0 |= (left == facesLeft) ? THRUST : REVERSE;
    }

    // action key edge: hyperspace with a direction held, else smart bomb
    const bool action = keys & actionKey;
    if (action && !actionLast) {
      pulseBits = (left || right) ? HYPERSPACE : SMART_BOMB;
      pulse = PULSE_FRAMES;
    }
    actionLast = action;
    if (keys & BOARD_KEY_A) {
      in0 |= HYPERSPACE;
    }
    if (keys & BOARD_KEY_B) {
      in0 |= SMART_BOMB;
    }
    if (pulse > 0) {
      pulse--;
      in0 |= pulseBits;
    }
    return in0 | (in1 << 8);
  }

private:
  bool actionLast = false;
  int pulse = 0;
  uint8_t pulseBits = 0;
};

#endif
