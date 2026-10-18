#ifndef SPEECH_RING_H
#define SPEECH_RING_H

#include <atomic>
#include <stdint.h>

// Hands speech samples from the emulation core to the audio core and
// resamples them to the audio rate.
//
//   emulation core               audio core
//   chip update(): push() ---->  render(): linear interpolation,
//   source rate, per emulated      step nudged by the fill level
//   frame in bursts
//
// Emulation runs at the 16 ms video pace, not exactly the source rate, so
// a fixed step would overflow or underrun mid-phrase. The step follows
// the smoothed fill level around TARGET instead. Single producer, single
// consumer; one slot stays empty.
class SpeechRing {
public:
  static const uint16_t SIZE = 1024;
  static const uint16_t TARGET = 384; // fill kept, about 46 ms at 8400 Hz

  // both cores stopped
  void reset(uint32_t in_hz, uint32_t out_hz) {
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
    nominal = (uint32_t)(((uint64_t)in_hz << 16) / out_hz);
    frac = 0;
    s0 = s1 = 0;
    avg_q4 = TARGET << 4;
    primed = false;
  }

  // emulation core; drops the sample when full
  void push(int16_t s) {
    const uint16_t h = head.load(std::memory_order_relaxed);
    const uint16_t next = (h + 1) & (SIZE - 1);
    if (next == tail.load(std::memory_order_acquire)) {
      return;
    }
    slot[h] = s;
    head.store(next, std::memory_order_release);
  }

  // audio core: n output samples. Silent until TARGET samples are queued,
  // again after an underrun.
  void render(int16_t *out, int n) {
    const uint16_t fill = count();
    if (!primed && fill < TARGET) {
      for (int i = 0; i < n; i++) {
        out[i] = s1;
      }
      return;
    }
    primed = true;

    const uint32_t step = rate(fill);
    for (int i = 0; i < n; i++) {
      // Q15 fraction: the product stays inside int32
      out[i] = s0 + (((int32_t)(s1 - s0) * (int32_t)(frac >> 1)) >> 15);
      frac += step;
      while (frac >= ONE) {
        frac -= ONE;
        s0 = s1;
        if (!pop(s1)) {
          primed = false; // hold the last sample
        }
      }
    }
  }

  uint16_t count() const {
    const uint16_t h = head.load(std::memory_order_acquire);
    return (h - tail.load(std::memory_order_relaxed)) & (SIZE - 1);
  }

private:
  static const uint32_t ONE = 1 << 16;      // Q16 step / phase
  static const int32_t GAIN_SHIFT = 11;     // +1 sample above TARGET: +1/2048 rate
  static const int32_t AVG_SHIFT = 5;       // fill low pass, about 85 ms at 375 calls/s
  static const int32_t MAX_DEVIATION = 10;  // step within nominal +-1/10

  bool pop(int16_t &s) {
    const uint16_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) {
      return false;
    }
    s = slot[t];
    tail.store((t + 1) & (SIZE - 1), std::memory_order_release);
    return true;
  }

  // Q16 step for this render call: nominal, plus the smoothed fill error
  uint32_t rate(uint16_t fill) {
    avg_q4 += (((int32_t)fill << 4) - avg_q4) >> AVG_SHIFT;
    const int32_t err = (avg_q4 >> 4) - TARGET;
    int32_t delta = ((int32_t)nominal * err) >> GAIN_SHIFT;
    const int32_t limit = nominal / MAX_DEVIATION;
    delta = delta > limit ? limit : (delta < -limit ? -limit : delta);
    return nominal + delta;
  }

  std::atomic<uint16_t> head{0};
  std::atomic<uint16_t> tail{0};
  int16_t slot[SIZE] = {};

  // audio core only
  uint32_t nominal = 0, frac = 0;
  int16_t s0 = 0, s1 = 0;
  int32_t avg_q4 = TARGET << 4;
  bool primed = false;
};

#endif
