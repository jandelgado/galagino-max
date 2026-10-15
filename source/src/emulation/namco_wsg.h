#ifndef NAMCO_WSG_H
#define NAMCO_WSG_H

#include <stdint.h>

typedef signed char NamcoWave[32];  // one 4-bit waveform from the sound PROM

// Namco WSG, 3 voices (Pac-Man, Dig Dug, Galaga, Xevious). Nibble
// registers at regs[0x05..0x1f] (MAME namco.cpp pacman layout). Output up
// to +-(3 * 15 * 128), not clipped.
class NamcoWsg {
public:
  static const int REG_COUNT = 0x20;
  void render(const uint8_t *regs, const NamcoWave *waves, int16_t *out, int n);

private:
  static const int VOICES = 3;
  uint32_t cnt[VOICES] = {0, 0, 0};
};

// Namco 15XX, 8 voices (Mappy, Gaplus, Tower of Druaga, Phozon). Voice v
// at regs[v * 8]: +3 volume, +4/+5 frequency bits 0-15, +6 waveform (bits
// 4-6) and frequency bits 16-19. Clock 18.432 MHz / 768 = 24 kHz exactly:
// one tick per output sample. Output: the raw voice sum up to
// +-(8 * 15 * 128); the machine adds its extras and halves it.
class Namco15xx {
public:
  static const int REG_COUNT = 0x40;
  void render(const uint8_t *regs, const NamcoWave *waves, bool enabled, int16_t *out, int n);

private:
  static const int VOICES = 8;
  uint32_t cnt[VOICES] = {0, 0, 0, 0, 0, 0, 0, 0};
};

#endif
