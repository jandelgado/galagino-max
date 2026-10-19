#ifndef SYSTEM1_PPI_H
#define SYSTEM1_PPI_H

#include <stdint.h>

// Intel 8255 as wired on System 1 (MAME i8255.cpp, system1.cpp sys1ppi):
// port A = sound latch, B = video mode, C = sound control, PC7 = sound NMI
// (active low). Star Jacker runs group A in mode 2 (control 0xc0):
//
//   main: OUT (14h) -> latch, /OBF = PC7 low -> sound NMI
//   sound NMI handler: LD A,(E000h) -> ackA(), /OBF high again
//
// In mode 0, PC7 is plain port C bit 7.
class Ppi8255 {
public:
  // all ports input: MAME tri-state values A 0x00, B 0x40, C 0x80
  void reset() {
    control = RESET_MODE;
    latch[PORT_A] = 0x00;
    latch[PORT_B] = 0x40;
    latch[PORT_C] = 0x80;
    obf = false;
    nmi = false;
  }

  void write(uint8_t port, uint8_t v) {
    const bool before = pc7();

    if (port == PORT_A) {
      latch[PORT_A] = v;
      obf = true;
    } else if (port == PORT_B || port == PORT_C) {
      latch[port] = v;
    } else if (v & MODE_SET) {
      control = v;
      latch[PORT_A] = latch[PORT_B] = latch[PORT_C] = 0;
      obf = false;
    } else {
      const uint8_t mask = 1 << ((v >> 1) & 7);
      latch[PORT_C] = (v & 1) ? (latch[PORT_C] | mask) : (latch[PORT_C] & ~mask);
    }

    edge(before);
  }

  // mode 2 port C reads status: /OBF in bit 7, group B bits from the latch
  uint8_t read(uint8_t port) const {
    if (port == PORT_C && obfMode()) {
      return (latch[PORT_C] & GROUP_B_BITS) | (obf ? 0 : PC7);
    }
    return port < CONTROL ? latch[port] : 0xff;
  }

  // sound CPU reads the latch: acknowledge, /OBF goes high
  uint8_t ackA() {
    obf = false;
    return latch[PORT_A];
  }

  // true once per PC7 falling edge
  bool takeNmi() {
    const bool n = nmi;
    nmi = false;
    return n;
  }

  uint8_t portB() const { return latch[PORT_B]; }
  uint8_t portC() const { return latch[PORT_C]; }

private:
  static constexpr uint8_t PORT_A = 0;
  static constexpr uint8_t PORT_B = 1;
  static constexpr uint8_t PORT_C = 2;
  static constexpr uint8_t CONTROL = 3;
  static constexpr uint8_t RESET_MODE = 0x9b;
  static constexpr uint8_t MODE_SET = 0x80;
  static constexpr uint8_t GROUP_A_MODE = 0x60;  // 00 mode 0, 01 mode 1, 1x mode 2
  static constexpr uint8_t MODE_1 = 0x20;
  static constexpr uint8_t PORT_A_INPUT = 0x10;
  static constexpr uint8_t GROUP_B_BITS = 0x07;
  static constexpr uint8_t PC7 = 0x80;

  // port A strobed output (mode 1 out, mode 2): PC7 is /OBF
  bool obfMode() const {
    const uint8_t mode = control & GROUP_A_MODE;
    return mode > MODE_1 || (mode == MODE_1 && !(control & PORT_A_INPUT));
  }

  bool pc7() const { return obfMode() ? !obf : (latch[PORT_C] & PC7); }

  void edge(bool before) {
    if (before && !pc7()) {
      nmi = true;
    }
  }

  uint8_t control;
  uint8_t latch[3];
  bool obf;  // output buffer full: /OBF (PC7) low
  bool nmi;
};

#endif
