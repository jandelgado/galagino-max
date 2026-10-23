#ifndef MARBLE_OPM_H
#define MARBLE_OPM_H

#include <stdint.h>
#include "../../emulation/ym2203fm.h"

// The sound board's YM2151 (OPM) as a YM2203 (OPN) (MARBLE.md "Sound"). Both are ymfm's
// 4-operator engine with the same envelopes and algorithms; Marble's effects use OPM channels
// 5-7 only, the YM2203 has 3. Register writes are translated:
//
//   OPM                         YM2203 (Ym2203Fm at 4.027 MHz: the OPM's 55.9 kHz rate)
//   channels 5, 6, 7            channels 0, 1, 2; the others are dropped (music: C64 tune)
//   key code / fraction         F-number and block
//   DT2 (coarse detune)         the nearest MUL
//   LFO pitch modulation        F-number writes once per frame (frame())
//   RL (output enable)          carrier TL: both 0 dB, one -6 dB (mono mix), none silent
//   LFO amplitude, noise        dropped (Marble's effects use no noise)
class MarbleOpm {
public:
  explicit MarbleOpm(Ym2203Fm &fm) : fm(fm) {}

  // emulation core: chip and translator back to power-on
  void reset();
  // emulation core: one OPM register write
  void write(uint8_t reg, uint8_t value);
  // emulation core: once per 60 Hz frame, LFO pitch modulation
  void frame();

private:
  static const uint8_t FIRST = 5;     // OPM channel of YM2203 channel 0
  static const uint8_t CHANNELS = 3;

  Ym2203Fm &fm;
  uint8_t regs[256];          // OPM register shadow
  uint8_t pmd;                // LFO pitch depth (register 0x19, bit 7 set)
  uint32_t lfo_phase;         // 30 bit cycle
  int16_t pm[CHANNELS];       // pitch offset in 1/64 semitone

  void pitch(uint8_t c);
  void opMul(uint8_t c, uint8_t k);
  void opLevel(uint8_t c, uint8_t k);
  void opWrite(uint8_t reg, uint8_t value);
  int16_t lfoWave() const;
};

#endif
