#ifndef ASTROCADE_SOUND_H
#define ASTROCADE_SOUND_H

#include <stdint.h>

// Bally Astrocade custom I/O sound (MAME astrocde.cpp astrocade_io_device):
// three square tones, a 15 bit noise LFSR and vibrato, all derived from a
// master oscillator. Clocked at 14.318 MHz / 8 = 1.789 MHz, so one 24 kHz
// output sample spans 74 or 75 chip clocks; render() box-filters over them.
//
// Registers (write): 0 master frequency, 1-3 tone A-C frequency, 4 vibrato
// speed (7-6) and depth (5-0), 5 tone C volume (3-0), mux (4: 0 vibrato,
// 1 noise), noise AM (5), 6 tone B volume (7-4) / A volume (3-0), 7 noise
// volume. Tone frequency: 1789773 / (2 * (reg0 + 1) * (regN + 1)) Hz.
//
// write() runs on the emulation core, render() on the video core, without
// a lock (like the Ay8910 register bytes). Output is unipolar,
// 0 .. MAX_LEVEL * gain.
class AstrocadeSound {
public:
  static const int MAX_LEVEL = 60;  // 15 * 4 sources
  static const uint32_t CHIP_HZ = 1789773;

  void reset();
  void write(uint8_t reg, uint8_t v) { regs[reg & 7] = v; }

  // adds n samples at 24 kHz to acc; gain = output counts per level step
  void render(int32_t *acc, int n, int gain);

  // 15 bit LFSR step (MAME formula), exposed for tests
  static uint16_t noiseNext(uint16_t s) {
    return (uint16_t)((s << 1) | (~((s >> 14) ^ (s >> 13)) & 1));
  }

private:
  static const int TONES = 3;

  // square tone: flips after `left` more master overflows, then every
  // reg + 1 overflows (MAME counter reloaded with ~reg)
  struct Tone {
    uint16_t left = 256;
    bool on = false;
  };

  // master overflows inside a span: first at clock `first`, then every
  // `period` clocks; `count` of them before the span end
  struct Overflows {
    int32_t first, period, count;
  };

  static int32_t toneSpan(Tone &t, uint16_t every, const Overflows &o, int32_t len);
  static void toneStep(Tone &t, uint16_t every);
  uint8_t reload() const;
  int32_t span(int32_t len, bool &overflowAtEnd);

  volatile uint8_t regs[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t master = 0;
  uint8_t noiseClock = 0;
  Tone tone[TONES];
  uint16_t noise = 0;
  uint16_t vibrato = 0;
  uint32_t frac = 0;  // Q16 chip clocks owed to the next output sample
};

#endif
