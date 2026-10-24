#ifndef AY8910_H
#define AY8910_H

#include <stdint.h>
#include "audio_mix.h"

// General Instrument AY-3-8910 PSG, also the SSG part of the YM2203: three
// square-wave tones, a noise LFSR and one envelope generator. render()
// reads the 16 register bytes the CPU wrote (R0-R15). Output at the chip's
// own scale, up to +-(3 * 15 * gain), not clipped.
class Ay8910 {
public:
  static const int REG_COUNT = 16;

  static const uint8_t NO_NOISE = 1;        // noise never reaches the outputs (MotoRace drone)
  static const uint8_t NO_PERIOD0_ENV = 2;  // envelope mode does not play period 0 tones (Gyruss)

  // inc: chip clock per output sample in counter units (clock / 8 / 24 kHz)
  // gain: output per volume step
  Ay8910(uint8_t inc, uint8_t gain, uint8_t flags = 0);

  void render(const uint8_t *regs, int16_t *out, int n);

  // scale voice ch by g / 16 (16 = 1.0), e.g. MCR SSIO duty-cycle volume
  void setChannelGain(int ch, uint8_t g) { chanGain[ch] = gain * g / 16; }

  // sum of N chips, chip c's registers at regs[c * REG_COUNT]
  template <int N>
  static void renderBank(Ay8910 (&chips)[N], const uint8_t *regs, int16_t *out, int n) {
    chips[0].render(regs, out, n);
    for (int c = 1; c < N; c++) {
      int16_t part[AUDIO_CHUNK];
      chips[c].render(regs + c * REG_COUNT, part, n);
      audioAdd(out, part, n);
    }
  }

private:
  static const int TONES = 3;
  static const int NOISE = 3;  // index of the noise generator in cnt/period
  static const int ENV_MAX = 15;

  void stepEnvelope(void);

  uint8_t inc;
  uint8_t gain;
  uint8_t flags;
  int16_t chanGain[3];  // per-voice output per volume step
  int volume[TONES] = {0, 0, 0};
  int enable[TONES] = {0, 0, 0};  // bit 0 tone, bit 1 noise
  bool envelope[TONES] = {false, false, false};
  int toggle[TONES] = {1, 1, 1};
  int cnt[TONES + 1] = {1, 1, 1, 1};
  int period[TONES + 1] = {0, 0, 0, 0};
  uint32_t noise_rng = 1;
  int env_period = 0;
  uint8_t env_shape = 0xff;   // none yet: the first render sets it
  int env_counter = 0;
  int env_step = 0;           // ENV_MAX down to 0; level = step ^ attack
  int env_attack = 0;         // 0 (decay) or ENV_MAX (attack)
  int env_hold = 0;
  int env_alternate = 0;
  int env_holding = 0;
};

#endif
