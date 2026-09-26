#ifndef ER2055_H
#define ER2055_H

#include <stdint.h>
#include <string.h>

// ER2055 64 byte EAROM (MAME er2055_device) as wired on Atari Centipede and
// Millipede: control byte CK = DB0, C1 = !DB1, C2 = DB2, CS1 = DB3, CS2 = 1.
// Volatile: contents start erased (0xff) on every power up.
class Er2055 {
public:
  static const uint8_t SIZE = 64;

  Er2055() { memset(data, 0xff, sizeof(data)); }

  // MAME machine reset: earom_control_w(0)
  void reset() { control(0); }

  // address and data latch (EAROM write strobe)
  void latch(uint8_t address, uint8_t value) {
    addr = address & (SIZE - 1);
    bus = value;
  }

  uint8_t read() const { return bus; }

  // control register write
  void control(uint8_t v) {
    uint8_t old = state;
    state = (old & CK) | ((v & 2) ? 0 : C1) | ((v & 4) ? C2 : 0) | ((v & 8) ? CS1 : 0) | CS2;
    if ((state & SEL) == SEL && state != old) {
      update();
    }

    // operations happen on the falling clock edge while selected
    old = state;
    state = (v & 1) ? (state | CK) : (state & ~CK);
    if ((state & SEL) != SEL || state == old || (v & 1)) {
      return;
    }
    if (state & C1) {
      bus = data[addr]; // read mode
    }
    update();
  }

private:
  enum : uint8_t { CK = 1, C1 = 2, C2 = 4, CS1 = 8, CS2 = 16, SEL = CS1 | CS2 };
  uint8_t data[SIZE];
  uint8_t addr = 0, bus = 0, state = 0;

  void update() {
    switch (state & (C1 | C2)) {
      case 0: data[addr] &= bus; break;  // write, needs prior erase
      case C2: data[addr] = 0xff; break; // erase
    }
  }
};

#endif
