#ifndef SN76489_H
#define SN76489_H

#include <stdint.h>
#include "audio_mix.h"

// TI SN76489: three square tones and a noise LFSR. The machine's port
// decoder writes period/volume directly (decoders differ per board).
// Output at the chip's scale, up to +-(4 * 15 * 6).
class Sn76489 {
public:
  // counter units per output sample: chip clock / 16 / 24 kHz
  static const int INC_4MHZ = 11;
  static const int INC_2MHZ = 5;     // also 1.79 MHz
  static const int INC_1536KHZ = 4;
  static const int CHANNELS = 4;     // 3 tones + noise
  static const int NOISE = 3;

  explicit Sn76489(int inc = INC_4MHZ);

  // register state; volume 15 = off. A channel with hold > 0 plays at
  // min_volume for hold more render() calls (short blips stay audible).
  int period[CHANNELS] = {0, 0, 0, 0};
  int volume[CHANNELS] = {15, 15, 15, 15};
  int min_volume[CHANNELS] = {15, 15, 15, 15};
  int hold[CHANNELS] = {0, 0, 0, 0};

  void render(int16_t *out, int n);

  template <int N>
  static void renderBank(Sn76489 (&chips)[N], int16_t *out, int n) {
    chips[0].render(out, n);
    for (int c = 1; c < N; c++) {
      int16_t part[AUDIO_CHUNK];
      chips[c].render(part, n);
      audioAdd(out, part, n);
    }
  }

private:
  int inc;
  int counter[CHANNELS] = {0, 0, 0, 0};
  int toggle[CHANNELS] = {1, 1, 1, 1};
  uint32_t noise_lfsr = 0x4000;
};

#endif
