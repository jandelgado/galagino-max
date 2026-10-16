#ifndef SN76489_H
#define SN76489_H

#include <stdint.h>
#include "audio_mix.h"

// TI SN76489: three square tones and a noise LFSR. The machine forwards the
// CPU's port bytes to write().
// Output at the chip's scale, up to +-(4 * 15 * 6).
class Sn76489 {
public:
  // counter units per output sample: chip clock / 16 / 24 kHz, in 1/FRAC
  // steps (a period of p toggles after p * FRAC counted down)
  static const int FRAC = 64;
  static const int INC_4MHZ = 667;           // 4 MHz: 10.417 * FRAC
  static const int INC_2MHZ = 333;           // 2 MHz: 5.208 * FRAC
  static const int INC_1789KHZ = 298;        // 14.318 MHz / 8: 4.661 * FRAC
  static const int INC_1536KHZ = 4 * FRAC;
  static const int CHANNELS = 4;     // 3 tones + noise
  static const int NOISE = 3;

  // a note shorter than one render() still sounds for HOLD_RENDERS renders
  // (render reads registers every 2.7 ms)
  static const uint8_t HOLD_SHORT_NOTES = 1;

  explicit Sn76489(int inc = INC_4MHZ, uint8_t flags = 0);

  // a port byte as the CPU wrote it (emulation core)
  void write(uint8_t data);

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
  static const uint8_t LATCH = 0x80;
  static const int TONE2 = 2;
  static const int NOISE_BASE = 4;    // N/512 in render() noise units
  static const int FOLLOW_TONE2 = 3;  // noise rate: tone 2 output
  static const int HOLD_RENDERS = 6;  // ~16 ms
  static const int OFF = 15;

  void setVolume(int ch, int vol);
  void updateNoisePeriod(void);

  int inc;
  uint8_t flags;
  uint8_t latch = 0;      // last latched register: channel * 2 + volume flag
  uint8_t noise_reg = 0;  // bits 0-1 rate, bit 2 white noise (not modeled)
  // register state; volume 15 = off. A channel with hold > 0 plays at
  // min_volume for hold more render() calls (short blips stay audible).
  int period[CHANNELS] = {0, 0, 0, 0};
  int volume[CHANNELS] = {15, 15, 15, 15};
  int min_volume[CHANNELS] = {15, 15, 15, 15};
  int hold[CHANNELS] = {0, 0, 0, 0};
  int counter[CHANNELS] = {0, 0, 0, 0};
  int toggle[CHANNELS] = {1, 1, 1, 1};
  uint32_t noise_lfsr = 0x4000;
};

#endif
