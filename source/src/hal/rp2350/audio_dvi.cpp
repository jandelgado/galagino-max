// HAL_AUDIO_DVI: sound over the HDMI cable. write() queues the chunk for the
// DVI scanout IRQ, which sends it as HDMI audio packets; the scanout is the
// clock, so a full ring paces the mixer as a real sink would.
// -D HDMI_TEST_TONE replaces the sound with a 1 kHz sine (attract modes are
// mostly silent).
#ifdef HAL_RP2350
#include "../../config.h"  // the board profile selects the backend
#ifdef HAL_AUDIO_DVI
#include "audio_dvi.h"
#include "../audio.h"
#include "rtos.h"

namespace hal {

namespace hdmi {
SampleRing audioRing;
}

namespace {
uint32_t chunk = 64;

#ifdef HDMI_TEST_TONE
const uint32_t TONE_PERIOD = 24;  // 1 kHz at 24 kHz
const int16_t TONE[TONE_PERIOD] = {
  0, 2588, 5000, 7071, 8660, 9659, 10000, 9659, 8660, 7071, 5000, 2588,
  0, -2588, -5000, -7071, -8660, -9659, -10000, -9659, -8660, -7071, -5000, -2588,
};
int16_t tone[64];
uint32_t phase;
#endif
}

void AudioOut::init(const AudioConfig &cfg) {
  chunk = cfg.chunk;
}

bool AudioOut::write(const int16_t *samples) {
#ifdef HDMI_TEST_TONE
  (void)samples;
  for (uint32_t i = 0; i < chunk; i++) {
    tone[i] = TONE[(phase + i) % TONE_PERIOD];
  }
  if (!hdmi::audioRing.push(tone, chunk)) {
    return false;
  }
  phase = (phase + chunk) % TONE_PERIOD;
  return true;
#else
  return hdmi::audioRing.push(samples, chunk);
#endif
}

void AudioOut::writeBlocking(const int16_t *samples) {
  while (!write(samples)) {
    sleepMs(1);
  }
}

void AudioOut::flushSilence() {
  static const int16_t silence[64] = {0};
  while (hdmi::audioRing.push(silence, sizeof(silence) / sizeof(silence[0]))) {
  }
}

} // namespace hal

#endif // HAL_AUDIO_DVI
#endif // HAL_RP2350
