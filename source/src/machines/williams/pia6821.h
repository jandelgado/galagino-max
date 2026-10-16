#ifndef PIA6821_H
#define PIA6821_H

#include <stdint.h>

// Motorola 6821 PIA, the subset the Williams boards use: ports with DDR,
// C1 edge IRQ flags, CB2 as manual output (Joust input mux select).
// C2 inputs and handshake modes are not wired on these boards.
class Pia6821 {
public:
  enum Port : uint8_t { PORT_NONE, PORT_A, PORT_B };

  void reset() { *this = Pia6821(); }
  void setInputs(uint8_t a, uint8_t b) {
    inA = a;
    inB = b;
  }

  // reg = address & 3: 0 ORA/DDRA, 1 CRA, 2 ORB/DDRB, 3 CRB
  uint8_t read(uint8_t reg) {
    switch (reg & 3) {
      case 0: return readPort(crA, ddrA, orA, inA);
      case 1: return crA;
      case 2: return readPort(crB, ddrB, orB, inB);
      default: return crB;
    }
  }

  // Returns the port whose output register was written (board side effects)
  Port write(uint8_t reg, uint8_t val) {
    switch (reg & 3) {
      case 0: return writePort(crA, ddrA, orA, val, PORT_A);
      case 1: crA = (crA & FLAGS) | (val & ~FLAGS); return PORT_NONE;
      case 2: return writePort(crB, ddrB, orB, val, PORT_B);
      default: crB = (crB & FLAGS) | (val & ~FLAGS); return PORT_NONE;
    }
  }

  void ca1(bool level) { edge(crA, ca1Level, level); }
  void cb1(bool level) { edge(crB, cb1Level, level); }

  bool irq() const { return pending(crA) || pending(crB); }
  uint8_t outA() const { return orA & ddrA; }
  // port B pins: those set as inputs float high (sound command idle 0xff)
  uint8_t outB() const { return (orB & ddrB) | (uint8_t)~ddrB; }

  // CA2 level in manual output mode (CRA b5 = b4 = 1), else low
  bool ca2() const { return (crA & C2_MANUAL) == C2_MANUAL && (crA & C2_LEVEL); }

  // CB2 level in manual output mode (CRB b5 = b4 = 1), else low
  bool cb2() const { return (crB & C2_MANUAL) == C2_MANUAL && (crB & C2_LEVEL); }

private:
  static const uint8_t IRQ1_EN = 0x01, IRQ1_RISE = 0x02, OR_SELECT = 0x04;
  static const uint8_t C2_LEVEL = 0x08, C2_MANUAL = 0x30, IRQ1_FLAG = 0x80;
  static const uint8_t FLAGS = 0xc0;

  static bool pending(uint8_t cr) { return (cr & IRQ1_FLAG) && (cr & IRQ1_EN); }

  static uint8_t readPort(uint8_t &cr, uint8_t ddr, uint8_t out, uint8_t in) {
    if (!(cr & OR_SELECT)) {
      return ddr;
    }
    cr &= ~FLAGS;  // reading the data register acknowledges the IRQ
    return (in & ~ddr) | (out & ddr);
  }

  static Port writePort(uint8_t cr, uint8_t &ddr, uint8_t &out, uint8_t val, Port port) {
    if (!(cr & OR_SELECT)) {
      ddr = val;
      return PORT_NONE;
    }
    out = val;
    return port;
  }

  static void edge(uint8_t &cr, bool &last, bool level) {
    const bool rising = !last && level, falling = last && !level;
    last = level;
    if ((cr & IRQ1_RISE) ? rising : falling) {
      cr |= IRQ1_FLAG;
    }
  }

  uint8_t orA = 0, ddrA = 0, crA = 0, inA = 0xff;
  uint8_t orB = 0, ddrB = 0, crB = 0, inB = 0xff;
  bool ca1Level = false, cb1Level = false;
};

#endif
