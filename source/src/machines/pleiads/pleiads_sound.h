#ifndef PLEIADS_SOUND_H
#define PLEIADS_SOUND_H

#include <stdint.h>
#include "../../emulation/dc_block.h"

// Pleiads sound board, port of MAME phoenix/pleiads.cpp (pleiads_sound_device)
// plus the TMS3615 single-note mode of sound/tms36xx.cpp. regs[0] latch A
// (0x6000), regs[1] latch B (0x6800, TMS3615 note), regs[2] latch C (video
// register bits 4/5). Rendered at 24 kHz on the video core.
class PleiadsSound {
public:
  PleiadsSound();
  void render(const uint8_t *regs, int16_t *out, int n);

private:
  struct Tone { int counter = 0, output = 0, max_freq = 0; };
  struct Cap { int counter = 0, level = 0; float charge = 0, discharge = 0; };

  // TMS3615 at 247 Hz, voices 16', 8', 4', 2' (MAME pleiads decays)
  class Tms3615 {
  public:
    void note(int octave, int note);
    int sample();   // 0 .. 32767

  private:
    static const int VOICES = 12;
    int vol[VOICES] = {}, vol_counter[VOICES] = {}, counter[VOICES] = {}, frequency[VOICES] = {};
    int output = 0, shift = 0, octave = 0, note_ofs = 0;
    bool restart = false, playing = false;
  };

  int tone1();
  int tone23();
  int tone4();
  int noise();
  enum Discharge { DISCHARGE_STEP, DISCHARGE_FAST };
  int charge(Cap &c, bool up, int vmin, Discharge mode = DISCHARGE_STEP);

  uint8_t latch_a = 0, latch_b = 0, latch_c = 0;
  bool first = true;
  Tone t1, t2, t3, t4;
  Cap pa5, pa6, pb4, pc4, pc5;
  int noise_counter = 0;
  uint32_t lfsr = 0;          // MAME poly18 generator state, polybit = output bit
  int polybit = 0;
  Tms3615 tms;
  DcBlock dc;
};

#endif
