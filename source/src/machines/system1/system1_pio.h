#ifndef SYSTEM1_PIO_H
#define SYSTEM1_PIO_H

#include <stdint.h>

// Z80 PIO as wired on System 1 (MAME z80pio.cpp, system1.cpp sys1pio):
// port A out = sound latch, ARDY = sound NMI, port B out = video mode.
// Offset bit 0 = port B, bit 1 = control. PIO interrupts are not wired.
//
//   main: OUT (18h) -> latch, ARDY low then high -> sound NMI
//   sound NMI handler: LD A,(E000h) -> ackA(), strobe drops ARDY
class Z80Pio {
public:
  void reset() {
    port[PORT_A] = Port();
    port[PORT_B] = Port();
    nmi = false;
  }

  void write(uint8_t offset, uint8_t v) {
    const uint8_t index = offset & PORT_SELECT;
    if (offset & CONTROL_SELECT) {
      control(index, v);
      return;
    }

    // output mode: MAME data_write drops RDY and raises it again, so every
    // write is an edge, even before the sound CPU acknowledged the last one
    port[index].out = v;
    if (port[index].mode == MODE_OUTPUT) {
      port[index].ready = false;
      raiseReady(index);
    }
  }

  uint8_t read(uint8_t offset) const {
    return (offset & CONTROL_SELECT) ? 0xff : port[offset & PORT_SELECT].out;
  }

  // sound CPU reads the latch: the strobe clears ARDY
  uint8_t ackA() {
    port[PORT_A].ready = false;
    return port[PORT_A].out;
  }

  // true once per ARDY rising edge
  bool takeNmi() {
    const bool n = nmi;
    nmi = false;
    return n;
  }

  uint8_t portB() const { return port[PORT_B].out; }

private:
  static constexpr uint8_t PORT_A = 0;
  static constexpr uint8_t PORT_B = 1;
  static constexpr uint8_t PORT_SELECT = 0x01;
  static constexpr uint8_t CONTROL_SELECT = 0x02;
  static constexpr uint8_t MODE_OUTPUT = 0;
  static constexpr uint8_t MODE_INPUT = 1;
  static constexpr uint8_t MODE_BIT = 3;
  static constexpr uint8_t WORD_TYPE = 0x0f;
  static constexpr uint8_t WORD_MODE = 0x0f;       // mmxx1111
  static constexpr uint8_t WORD_INTERRUPT = 0x07;  // xxxx0111, bit 4: mask follows
  static constexpr uint8_t MASK_FOLLOWS = 0x10;

  struct Port {
    uint8_t mode = MODE_INPUT;
    uint8_t out = 0;
    bool ready = false;
    bool maskNext = false;  // next control word is the I/O or interrupt mask
  };

  void control(uint8_t index, uint8_t v) {
    Port &p = port[index];
    if (p.maskNext) {
      p.maskNext = false;
      return;
    }

    const uint8_t type = v & WORD_TYPE;
    if (type == WORD_INTERRUPT) {
      p.maskNext = v & MASK_FOLLOWS;
      return;
    }
    if (type != WORD_MODE) {
      return;  // interrupt vector
    }

    // MAME set_mode: output asserts RDY, other modes leave it
    p.mode = v >> 6;
    p.maskNext = p.mode == MODE_BIT;
    if (p.mode == MODE_OUTPUT) {
      raiseReady(index);
    }
  }

  void raiseReady(uint8_t index) {
    if (!port[index].ready && index == PORT_A) {
      nmi = true;
    }
    port[index].ready = true;
  }

  Port port[2];
  bool nmi = false;
};

#endif
