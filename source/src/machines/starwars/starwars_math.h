#ifndef STARWARS_MATH_H
#define STARWARS_MATH_H

#include <stdint.h>

// Star Wars math board (MAME atari/starwars_m.cpp): matrix processor,
// divider and PRNG, main CPU 0x4700-0x4707. The matrix processor runs a
// PROM microcode program on the shared math RAM (0x5000-0x5fff):
//
//   ACC = ACC + (A - B) * C    (LDC strobe, serial multiplier)
//
// A run is instant here; MATH_RUN (IN1 bit 7) always reads done.
class StarWarsMath {
public:
  static const uint16_t RAM_SIZE = 0x1000;
  static const uint16_t PROM_SIZE = 0x400;

  // microcode: PROM_SIZE words, strobes 15-8, AM 7, address 6-0
  // ram: RAM_SIZE bytes, big endian words
  void init(const uint16_t *microcode, uint8_t *ram);
  void reset();
  // offset 0: run at PROM address v * 4, 1-2: BIC, 4-7: divisor, dividend
  void write(uint8_t offset, uint8_t v);
  uint8_t quotient_hi() const { return quotient >> 8; }
  uint8_t quotient_lo() const { return quotient & 0xff; }
  uint8_t random();

private:
  void run();
  void divide();

  const uint16_t *prom = nullptr;
  uint8_t *ram = nullptr;
  uint16_t mpa = 0, bic = 0;     // PROM address, block index counter
  uint16_t a = 0, b = 0, c = 0;  // multiplier inputs
  uint32_t acc = 0;              // 16 readable msb, 16 guard lsb
  uint16_t dividend = 0, divisor = 0, dvd_shift = 0, quotient = 0;
  uint32_t prng = 1;
};

#endif
