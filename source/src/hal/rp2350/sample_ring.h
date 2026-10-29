#ifndef HAL_RP2350_SAMPLE_RING_H
#define HAL_RP2350_SAMPLE_RING_H

// Audio samples from the mixer (video core) to the HDMI scanout IRQ
// (emulation core). One producer, one consumer, no lock: each side writes
// only its own index, a barrier orders the samples before the index. Never
// blocks, so the IRQ can use it. No SDK dependencies, so host tests cover it.

#include <stdint.h>

namespace hal {
namespace hdmi {

class SampleRing {
public:
  // 21 ms at 24 kHz: the mixer refills once per strip group, but not while
  // the video core waits for the beam at the end of a frame
  static const uint32_t SIZE = 512;

  // producer side: all of `n` or nothing
  bool push(const int16_t *samples, uint32_t n) {
    if (space() < n) {
      return false;
    }
    const uint32_t h = head;
    for (uint32_t i = 0; i < n; i++) {
      buf[(h + i) % SIZE] = samples[i];
    }
    __sync_synchronize();
    head = h + n;
    return true;
  }

  uint32_t space() const { return SIZE - (head - tail); }

  // consumer side: all of `n` or nothing; inlined into the IRQ (RAM)
  __attribute__((always_inline)) bool pop(int16_t *samples, uint32_t n) {
    const uint32_t t = tail;
    if (head - t < n) {
      return false;
    }
    __sync_synchronize();
    for (uint32_t i = 0; i < n; i++) {
      samples[i] = buf[(t + i) % SIZE];
    }
    __sync_synchronize();
    tail = t + n;
    return true;
  }

  // only while the consumer is idle (init, host tests)
  void clear() {
    head = 0;
    tail = 0;
  }

private:
  int16_t buf[SIZE];
  volatile uint32_t head = 0;  // next write, producer only
  volatile uint32_t tail = 0;  // next read, consumer only
};

} // namespace hdmi
} // namespace hal

#endif // HAL_RP2350_SAMPLE_RING_H
