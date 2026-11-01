// HAL_AUDIO_NULL on a device: silent sink with the timing of a real one,
// `buffers` chunks of backlog, each played out in chunk / rate seconds. The
// mixer refills only what write() accepts, so the emulated sound chips advance
// at the audio rate (a sink that always accepts would spin the frame loop).
#include "audio.h"
#if defined(HAL_AUDIO_NULL) && !defined(HAL_HOST)
#include "hal.h"

namespace hal {

namespace {

uint32_t chunkUs = 2667;
uint32_t buffers = 4;
uint32_t playUntil = 0;  // micros() when the queued chunks are played out

// Queued playing time at `now`, 0 once played out.
uint32_t queued(uint32_t now) {
  const int32_t left = (int32_t)(playUntil - now);
  return left > 0 ? (uint32_t)left : 0;
}

} // namespace

void AudioOut::init(const AudioConfig &cfg) {
  chunkUs = (uint32_t)((uint64_t)cfg.chunk * 1000000 / cfg.rate);
  buffers = cfg.buffers;
  playUntil = micros();
}

bool AudioOut::write(const int16_t *) {
  const uint32_t now = micros();
  const uint32_t backlog = queued(now);
  if (backlog > (buffers - 1) * chunkUs) {
    return false;
  }
  playUntil = now + backlog + chunkUs;
  return true;
}

void AudioOut::writeBlocking(const int16_t *samples) {
  while (!write(samples)) {
    sleepMs(1);
  }
}

void AudioOut::flushSilence() { playUntil = micros(); }

} // namespace hal
#endif // HAL_AUDIO_NULL && !HAL_HOST
