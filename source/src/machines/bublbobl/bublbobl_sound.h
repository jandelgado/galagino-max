#ifndef BUBLBOBL_SOUND_H
#define BUBLBOBL_SOUND_H

#include <atomic>
#include <stdint.h>
#include "../../emulation/ima_adpcm.h"

// Effect triggers (emulation core -> audio core) and a 2 channel
// IMA-ADPCM effect mixer, host tested.
//
//   emulation core            audio core (renderSid)
//   sound latch write  --->   FxRing  --->  FxMixer.start()
//                    push()          pop()  next() per 24 kHz sample
namespace bublbobl_sound {

// single producer, single consumer; one slot stays empty
struct FxRing {
  static const uint8_t SIZE = 8;
  std::atomic<uint8_t> head{0};
  std::atomic<uint8_t> tail{0};
  uint8_t slot[SIZE] = {};

  // drops the effect when full
  void push(uint8_t fx) {
    const uint8_t h = head.load(std::memory_order_relaxed);
    const uint8_t next = (uint8_t)((h + 1) % SIZE);
    if (next == tail.load(std::memory_order_acquire)) {
      return;
    }
    slot[h] = fx;
    head.store(next, std::memory_order_release);
  }

  bool pop(uint8_t &fx) {
    const uint8_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) {
      return false;
    }
    fx = slot[t];
    tail.store((uint8_t)((t + 1) % SIZE), std::memory_order_release);
    return true;
  }
};

struct FxSample {
  const uint8_t *data; // 2-byte LE predictor, then one nibble per sample
  uint32_t len;        // decoded samples
};

// an effect restarts its own channel if it still plays (rapid fire must
// not cut other effects), else takes a free channel, else the oldest
struct FxMixer {
  static const uint8_t CHANNELS = 2;

  struct Channel {
    const uint8_t *data = nullptr;
    uint32_t len = 0;
    uint32_t pos = 0;
    uint32_t age = 0;
    int16_t predictor = 0;
    int8_t step = 0;
    uint8_t fx = 0;
  };
  Channel ch[CHANNELS];
  uint32_t clock = 0;

  void start(const FxSample &s, uint8_t fx) {
    const uint8_t pick = channelFor(fx);

    Channel &c = ch[pick];
    c.data = s.data;
    c.len = s.len;
    c.pos = 0;
    c.age = ++clock;
    c.predictor = (int16_t)(s.data[0] | (s.data[1] << 8));
    c.step = 0;
    c.fx = fx;
  }

  uint8_t channelFor(uint8_t fx) const {
    for (uint8_t i = 0; i < CHANNELS; i++) {
      if (ch[i].data && ch[i].fx == fx) {
        return i;
      }
    }

    uint8_t pick = 0;
    for (uint8_t i = 0; i < CHANNELS; i++) {
      if (!ch[i].data) {
        return i;
      }
      if (ch[i].age < ch[pick].age) {
        pick = i;
      }
    }
    return pick;
  }

  void stopAll() {
    for (uint8_t i = 0; i < CHANNELS; i++) {
      ch[i].data = nullptr;
    }
  }

  // one 24 kHz sample, sum of the channels on the +-127 scale. Effects
  // are stored at 12 kHz: each stored sample is preceded by the midpoint
  // to the previous one (linear interpolation, half a sample late)
  int32_t next() {
    int32_t mix = 0;
    for (uint8_t i = 0; i < CHANNELS; i++) {
      Channel &c = ch[i];
      if (!c.data) {
        continue;
      }

      if (c.pos & 1) {
        mix += c.predictor >> 8;
      } else {
        // sample 0 is the predictor itself, then low nibble first
        const int16_t prev = c.predictor;
        const uint32_t s = c.pos >> 1;
        if (s > 0) {
          const uint32_t n = s - 1;
          const uint8_t packed = c.data[2 + (n >> 1)];
          ima_adpcm::ima_adpcm_decode(c.predictor, c.step, (n & 1) ? (packed >> 4) : (packed & 0x0f));
        }
        mix += (prev + c.predictor) >> 9;
      }

      if (++c.pos >= 2 * c.len) {
        c.data = nullptr;
      }
    }
    return mix;
  }
};

} // namespace bublbobl_sound

#endif
