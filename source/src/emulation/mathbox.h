#ifndef MATHBOX_H
#define MATHBOX_H

#include <stdint.h>

// Atari Mathbox (Battle Zone), port of MAME atari/mathbox.cpp: the
// microcoded bit slice ALU simulated per command, result ready at once
// (status always 0 = done). Commands are the write offset 0x00-0x1f.
class Mathbox {
public:
  void reset();
  void go(uint8_t offset, uint8_t data);
  uint8_t lo() const { return (uint16_t)result & 0xff; }
  uint8_t hi() const { return (uint16_t)result >> 8; }

private:
  int16_t reg[16];
  int16_t result;
};

#endif
