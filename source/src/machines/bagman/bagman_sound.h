#ifndef BAGMAN_SOUND_H
#define BAGMAN_SOUND_H

#include <stdint.h>

// Bagman tone generator: sine notes. regs[0..1] note duration (counted
// down here), regs[2] note code, regs[3] pause (counted down here).
class BagmanSound {
public:
  BagmanSound();
  void render(uint8_t *regs, int16_t *out, int n);

private:
  static const int WAVE_LEN = 256;
  int16_t sinus[WAVE_LEN];
  uint16_t position_last = 0;
};

#endif
