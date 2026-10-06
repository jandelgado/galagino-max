#ifndef ATETRIS_SLAPSTIC_H
#define ATETRIS_SLAPSTIC_H

#include <stdint.h>

// Atari slapstic 137412-101 (MAME atari/slapstic.cpp, slapstic101 table,
// 8 bit bus, range 0x6000-0x7fff, 13 address lines). MAME taps every bus
// access, read or write, anywhere in the address space: call access() for
// each one, after the access itself. Tetris uses reset, direct and bitwise
// switching; alt is kept so 0x7exx accesses break sequences as in MAME.
class Slapstic101 {
public:
  void reset() {
    state = IDLE;
    cur = 3;
    loaded = 0;
  }

  uint8_t bank() const { return cur; }

  void access(uint16_t a) {
    if (a == RESET) { // every state, including active, goes (or stays) active
      state = ACTIVE;
      return;
    }
    switch (state) {
      case IDLE:
        return;
      case ACTIVE:
        // direct: 0x6080, 0x6090, 0x60a0, 0x60b0 select bank 0..3
        if ((a & 0xffcf) == (RESET | 0x0080)) {
          cur = (a >> 4) & 3;
          state = IDLE;
          return;
        }
        if (in(a, 0x1f00, 0x1e00)) {
          state = ALT_VALID;
        } else if (in(a, 0x1ff0, 0x1540)) {
          state = BIT_LOAD;
        }
        return;
      case ALT_VALID: // the 2nd alt access must be outside the range
        state = (!inside(a) && (a & 0x1fff) == 0x1fff) ? ALT_SELECT : ACTIVE;
        return;
      case ALT_SELECT:
        if (in(a, 0x1ffc, 0x1b5c)) {
          loaded = a & 3;
          state = ALT_COMMIT;
        } else {
          state = ACTIVE;
        }
        return;
      case ALT_COMMIT:
        if (in(a, 0x1fcf, 0x0080)) {
          cur = loaded;
          state = IDLE;
        }
        return;
      case BIT_LOAD:
        if (in(a, 0x1fcf, 0x0080)) {
          loaded = cur;
          state = BIT_ODD;
        }
        return;
      case BIT_ODD:
      case BIT_EVEN:
        bit_set(a);
        return;
    }
  }

private:
  enum State : uint8_t { IDLE, ACTIVE, ALT_VALID, ALT_SELECT, ALT_COMMIT, BIT_LOAD, BIT_ODD, BIT_EVEN };
  static const uint16_t RESET = 0x6000;
  static const uint16_t RANGE_MASK = 0xe000; // ~(0x7fff - 0x6000)

  static bool inside(uint16_t a) { return (a & RANGE_MASK) == RESET; }
  static bool in(uint16_t a, uint16_t mask, uint16_t value) {
    return (a & (RANGE_MASK | mask)) == (RESET | value);
  }

  // bit ops alternate between odd and even meaning (slapstic101 bit3*)
  void bit_set(uint16_t a) {
    if (in(a, 0x1ff8, 0x1550)) {
      cur = loaded;
      state = IDLE;
      return;
    }
    // MAME tests 0x1540..0x1543 with mask 0x1ff3: bits 2-3 don't care
    if (!in(a, 0x1ff0, 0x1540)) {
      return;
    }
    // odd: 0 clear b0, 1 set b0, 2 clear b1, 3 set b1; even: reversed
    const uint8_t op = state == BIT_ODD ? (a & 3) : 3 - (a & 3);
    const uint8_t bit = op & 2 ? 2 : 1;
    loaded = op & 1 ? (loaded | bit) : (loaded & ~bit);
    state = state == BIT_ODD ? BIT_EVEN : BIT_ODD;
  }

  State state = IDLE;
  uint8_t cur = 3, loaded = 0;
};

#endif
