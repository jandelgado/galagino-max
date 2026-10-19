#ifndef PHOENIX_SOUND_H
#define PHOENIX_SOUND_H

#include <stdint.h>

// Phoenix discrete audio, port of MAME phoenix_a.cpp (phoenix_sound_device
// + phoenix_discrete netlist). regs[0] sound A latch (0x6000), regs[1]
// sound B latch (0x6800). Analog state in float, not double: the ESP32 FPU
// is single precision only.
class PhoenixSound {
public:
  void render(const uint8_t *regs, int16_t *out, int n);

private:
  int32_t c24_level = 0;
  int32_t c24_counter = 0;
  int32_t c25_level = 0;
  int32_t c25_counter = 0;
  uint32_t noise_shiftreg = 0x1FFFF;
  int32_t noise_counter = 0;
  int noise_polybit = 0;
  int32_t noise_lp_counter = 0;
  int noise_lp_polybit = 0;
  float e1_vc1 = 0;
  float e1_555_cap = 0;
  uint8_t e1_555_ff = 1;
  int e1_note_c1 = 0;
  int e1_note_c2 = 0;
  float e1_rcfilt = 0;
  float e2_555a_cap = 0;
  uint8_t e2_555a_ff = 1;
  float e2_555b_cap = 0;
  uint8_t e2_555b_ff = 1;
  float e2_rcfilt = 0;
  float e2_555cv_cap = 0;
  uint8_t e2_555cv_ff = 1;
  int e2_note_c1 = 0;
  int e2_note_c2 = 0;
  float mix_vcap1 = 0;
  float mix_vcap2 = 0;
  float mix_vcamp = 0;
  uint8_t mel_idx = 0;
  uint8_t mel_tune = 0;
  uint16_t mel_phase = 0;
  uint16_t mel_freq = 0;
  uint16_t mel_timer = 0;
  bool mel_active = false;
};

#endif
