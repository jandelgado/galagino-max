#ifndef ZAXXON_SOUND_H
#define ZAXXON_SOUND_H

#include <stdint.h>

// The ROM (0x0165) pulses the one-shot bits of ports B/C: writes ~request,
// then 0xff (or |0xf0) right after, so render-time sampling of the port
// latch never sees them low. zaxxon::wrZ80 counts each falling edge in
// soundregs[ZAXXON_TRIG_BASE + ch] (ch 4..9); ZaxxonSound starts a channel when
// its counter changed. Single writer, single reader, no lock needed.
static const uint8_t ZAXXON_TRIG_BASE = 4;

// Zaxxon sound board (MAME zaxxon_a.cpp): no tone chip, 12 discrete
// circuits, each triggered by one i8255 PPI output bit and played here as
// an IMA-ADPCM sample. regs[0..2] = PPI ports A/B/C. Output +-500.
class ZaxxonSound {
public:
  ZaxxonSound();
  void render(const uint8_t *regs, int16_t *out, int n);

private:
  static const int CHANNELS = 12;
  static const int TRIGGERED = 6;  // ports B/C one-shots, channels 4..9
  enum PlayMode : uint8_t { ONE_SHOT, LOOP };

  void startChannel(int ch, PlayMode mode);
  void stopChannel(int ch);

  // Port A (regs[0]) is edge-decoded each render call against prev, like
  // MAME's m_sound_state; trig_seen is the last counter value seen.
  const uint8_t *sample[CHANNELS];  // IMA-ADPCM stream per channel
  uint8_t prev = 0;
  uint8_t trig_seen[TRIGGERED] = {};
  bool active[CHANNELS] = {};
  bool loop[CHANNELS] = {};
  uint32_t pos[CHANNELS] = {};
  int16_t adpcm_predictor[CHANNELS] = {};
  int8_t adpcm_step[CHANNELS] = {};
  // samples stored below 24 kHz: output sample within the hold, and the
  // previous decoded sample to interpolate from
  uint8_t phase[CHANNELS] = {};
  int16_t adpcm_prev[CHANNELS] = {};
};

#endif
