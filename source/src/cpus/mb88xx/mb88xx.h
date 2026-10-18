/*
 * Fujitsu MB88xx 4-bit MCU, MB8844 variant (1 KB ROM, 64 nibbles RAM).
 * Port of MAME src/devices/cpu/mb88xx/mb88xx.cpp (BSD-3-Clause, Ernesto
 * Corvi). Used by the Namco 54xx explosion generator.
 *
 * Differences to MAME: no serial port (the 54xx never enables it), no
 * TC input, no 4-bit PLA. The board wiring comes from the virtual port
 * hooks of a subclass.
 */

#ifndef MB88XX_H
#define MB88XX_H

#include <stdint.h>

class Mb88 {
public:
  virtual ~Mb88() {}

  void reset();

  // rising edge requests the external interrupt, if enabled
  void set_irq(bool state);

  // runs whole instructions for `cycles` machine cycles (clock / 6);
  // the overshoot is taken from the next call
  void run(int cycles);

protected:
  explicit Mb88(const uint8_t *rom) : rom(rom) {}

  virtual uint8_t read_k() { return 0; }
  virtual void write_o(uint8_t value, uint8_t mask) {}
  virtual void write_p(uint8_t value) {}
  virtual uint8_t read_r(uint8_t port) { return 0; }
  virtual void write_r(uint8_t port, uint8_t value) {}

private:
  static const uint16_t ROM_MASK = 0x3ff;
  static const uint8_t RAM_SIZE = 64;

  uint8_t fetch() const { return rom[((PA << 6) + PC) & ROM_MASK]; }
  void inc_pc();
  uint8_t rdmem(uint8_t a) const { return ram[a & (RAM_SIZE - 1)] & 0x0f; }
  void wrmem(uint8_t a, uint8_t v) { ram[a & (RAM_SIZE - 1)] = v & 0x0f; }
  uint8_t ea() const { return (X << 4) + Y; }
  void write_pla(uint8_t index);
  void increment_timer();
  void burn_cycles(int cycles);
  void step();

  const uint8_t *rom;
  uint8_t ram[RAM_SIZE] = {};

  uint8_t PC = 0, PA = 0;   // 6-bit offset, 5-bit page
  uint16_t SP[4] = {};      // return address + saved flags (bits 13-15)
  uint8_t SI = 0;
  uint8_t A = 0, X = 0, Y = 0;
  uint8_t st = 1, zf = 0, cf = 0, vf = 0, sf = 0, irq_line = 0;
  uint8_t pio = 0;
  uint8_t TH = 0, TL = 0, TP = 0, SB = 0;
  uint8_t pending_irq = 0;
  bool in_irq = false;
  uint8_t o_output = 0;
  int icount = 0;
};

#endif
