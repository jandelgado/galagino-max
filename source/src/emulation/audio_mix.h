#ifndef AUDIO_MIX_H
#define AUDIO_MIX_H

#include <stdint.h>

// machineBase::renderAudio() contract: Audio::transmit() asks for
// AUDIO_CHUNK samples at 24 kHz per call; samples must stay within
// +-AUDIO_MAX (Audio's DAC scaling wraps beyond).
static const int AUDIO_CHUNK = 64;
static const int32_t AUDIO_MAX = 511;

static inline int16_t audioClamp(int32_t v, int32_t limit) {
  if (v > limit) {
    return limit;
  }
  if (v < -limit) {
    return -limit;
  }
  return v;
}

static inline void audioClampAll(int16_t *buf, int n, int32_t limit) {
  for (int i = 0; i < n; i++) {
    buf[i] = audioClamp(buf[i], limit);
  }
}

// dst += src, int16 wrap-around like the int16 accumulators it replaces
static inline void audioAdd(int16_t *dst, const int16_t *src, int n) {
  for (int i = 0; i < n; i++) {
    dst[i] += src[i];
  }
}

static inline void audioShiftRight(int16_t *buf, int n, int bits) {
  for (int i = 0; i < n; i++) {
    buf[i] = buf[i] >> bits;
  }
}

#endif
