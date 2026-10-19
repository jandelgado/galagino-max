#ifndef BERZERK_AUDIO_H
#define BERZERK_AUDIO_H

#include <stdint.h>

#include "../../emulation/exidy6840.h"
#include "../../emulation/s14001a.h"
#include "../../emulation/speech_ring.h"

// Berzerk sound board (MAME berzerk audio_w/audio_r): S14001A speech and
// Exidy 6840 effects. Both run on the emulation core, stepped per scanline
// with the Z80 so /BUSY polling and timer writes see the right time.
//
//   emulation core                         audio core
//   write()/read()  port 0x40-0x47
//   line()  -> S14001A + 6840 -> push --->  render(): SpeechRing
//              mixed at OUT_HZ                resampled to the I2S rate
class BerzerkAudio {
public:
  static const uint32_t OUT_HZ = 24000;
  static const uint32_t LINE_HZ = 15625; // 5 MHz pixel clock / 320

  void reset(const uint8_t *speech_rom); // both cores stopped
  void write(uint8_t offset, uint8_t data);
  uint8_t read(uint8_t offset);
  void line(); // one scanline elapsed: emits 1.536 samples on average
  void render(int16_t *out, int n) { ring.render(out, n); }
  bool speaking() const { return speech.busy(); }

private:
  void emit();

  S14001A speech;
  Exidy6840 sfx;
  uint32_t line_acc = 0;     // samples owed, in LINE_HZ units
  uint32_t speech_step = 0;  // Q16 chip clocks per output sample
  uint32_t speech_frac = 0;
  uint8_t speech_gain = 0;   // 0..7
  int32_t speech_level = 0;  // -7..8, held when a sample gets no chip clock
  SpeechRingT<2048, 1024> ring;
};

#endif
