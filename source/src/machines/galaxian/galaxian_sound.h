#ifndef GALAXIAN_SOUND_H
#define GALAXIAN_SOUND_H

#include <stdint.h>

// Galaxian / Moon Cresta discrete sound (MAME galaxian_a.cpp), register
// map in render(). Output up to +-310.
class GalaxianSound {
public:
  void render(const uint8_t *regs, int16_t *out, int n);

private:
  uint8_t lfo = 0;
  uint16_t lfo_counter = 0;
  uint8_t tone_cnt = 0;            // VCO tone phase accumulator
  int tone_toggle = 1;             // VCO square wave state
  uint8_t fs_cnt[3] = {0, 0, 0};   // FS1/FS2/FS3 phase accumulators
  int fs_toggle[3] = {1, 1, 1};    // FS1/FS2/FS3 square wave states
  uint32_t noise_rng = 1;          // noise LFSR (HIT)
  uint32_t fire_rng = 0x1234;      // noise LFSR (FIRE)
  int fire_cnt = 0;                // fire noise clock counter
  int last_pitch = 0xff;           // credit sound sweep detection
  int pitch_active = 0;
};

#endif
