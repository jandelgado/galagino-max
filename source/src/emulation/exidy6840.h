#ifndef EXIDY6840_H
#define EXIDY6840_H

#include <stdint.h>

// Exidy / Stern sound effects board: 6840 PTM, three square wave timers
// with per channel volume, optionally clocked by a 128 bit LFSR noise
// source (MAME mame/shared/exidysound.cpp, exidy_sound_device).
//
// MAME steps the timers at 1.79 MHz. Here sample() runs SUBSTEPS steps per
// output sample and averages them: same timer logic, cheap enough for the
// ESP32, with a little anti-aliasing.
class Exidy6840 {
public:
  static const uint32_t CLOCK_HZ = 894886; // 3.579545 MHz / 4
  static const int32_t BASE_VOLUME = 32767 / 6;
  static const int SUBSTEPS = 4;

  void reset(uint32_t sample_hz);
  void ptm_w(uint8_t offset, uint8_t data);     // 6840 register 0..7
  uint8_t ptm_r(uint8_t offset);
  void sfxctrl_w(uint8_t offset, uint8_t data); // 0: sfxctrl, 1..3: volume
  int32_t sample();                             // 0 .. 3 * BASE_VOLUME

private:
  struct Timer {
    uint8_t cr;
    uint8_t state;
    uint8_t leftovers;
    uint16_t timer;
    uint32_t clocks;
    uint16_t counter;
  };

  // CR bits
  enum : uint8_t {
    CR_ALL_RESET = 0x01, // ch0: hold all; ch1: offset 0 writes ch0; ch2: /8 prescale
    CR_INTERNAL = 0x02,  // clock source E (else external: noise)
    CR_DUAL_8BIT = 0x04,
    CR_NO_PRELOAD = 0x10,
    CR_OUTPUT = 0x80,
  };

  // sfxctrl bits
  enum : uint8_t {
    SFX_NOISE_FROM_CH0 = 0x01,
    SFX_MUTE_CH0 = 0x02,
  };

  static void apply_clock(Timer &t, int clocks);
  int update_noise(int clocks);
  int32_t step(int clocks);

  Timer m_timer[3];
  int32_t m_volume[3];
  uint8_t m_msb_latch, m_lsb_latch;
  uint32_t m_lfsr_oldxor, m_lfsr[4];
  uint32_t m_clocks_per_step; // 8.24 fixed point
  uint32_t m_clock_count;
  uint8_t m_sfxctrl;
};

#endif
