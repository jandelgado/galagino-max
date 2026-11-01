#ifndef HAL_HOST_AUDIO_H
#define HAL_HOST_AUDIO_H

#include <stdint.h>
#include <string.h>
#include "../audio_config.h"

namespace hal {
namespace host {

typedef void (*AudioHook)(const int16_t *samples, uint32_t n);

struct AudioState {
  uint32_t chunk = 64;
  uint32_t buffers = 4;
  uint32_t capacity = 0xFFFFFFFFu;  // writes write() still accepts
  uint32_t writes = 0;
  AudioHook hook = nullptr;
};

inline AudioState &_audioState() {
  static AudioState s;
  return s;
}

// Test control: how many writes the sink accepts before it reports "full",
// and a callback that sees every chunk (write, writeBlocking, flushSilence).
inline void setAudioCapacity(uint32_t writes) { _audioState().capacity = writes; }
inline void setAudioHook(AudioHook hook) { _audioState().hook = hook; }
inline uint32_t audioWrites() { return _audioState().writes; }

} // namespace host

class AudioOut {
public:
  static void init(const AudioConfig &cfg) {
    host::_audioState().chunk = cfg.chunk;
    host::_audioState().buffers = cfg.buffers;
  }

  static bool write(const int16_t *samples) {
    host::AudioState &s = host::_audioState();
    if (s.capacity == 0) {
      return false;
    }
    if (s.capacity != 0xFFFFFFFFu) {
      s.capacity--;
    }
    deliver(samples);
    return true;
  }

  static void writeBlocking(const int16_t *samples) { deliver(samples); }

  static void flushSilence() {
    host::AudioState &s = host::_audioState();
    int16_t silence[256];
    memset(silence, 0, sizeof(silence));
    for (uint32_t i = 0; i < s.buffers; i++) {
      deliver(silence);
    }
  }

private:
  static void deliver(const int16_t *samples) {
    host::AudioState &s = host::_audioState();
    s.writes++;
    if (s.hook) {
      s.hook(samples, s.chunk);
    }
  }
};

} // namespace hal

#endif // HAL_HOST_AUDIO_H
