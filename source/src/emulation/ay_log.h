#ifndef AY_LOG_H
#define AY_LOG_H

#include <algorithm>
#include <atomic>
#include <stdint.h>
#include <string.h>

#include "audio_mix.h"

// Hands AY register writes from the emulation core to the audio side with
// their time inside the emulated frame.
//
// Sampling the registers once per audio chunk (6 per frame) loses fast
// writes: volume steps used as a software waveform, pitch sweeps that move
// every 0.3 ms. The log replays them in the slice they happened in.
//
//   emulation core (run_frame)          audio core (renderAudio)
//     beginFrame(steps)                   play(out, n, render):
//     at(step); write(reg, val) ...         per 8-sample slice:
//     endFrame()  -- ring of writes -->       apply due writes to regs
//                                             render(regs, out, 8)
//
// One frame is FRAME_SAMPLES long on the audio side. The audio side leaves
// a frame only at its end marker, so it follows the emulation when that
// runs slow (registers hold) and plays queued frames in order when it runs
// ahead. The two clocks differ a little (60 Hz display pacing against the
// I2S clock) and the emulation boots at full speed, so the lead grows:
// beyond MAX_LEAD queued frames the audio side skips frames down to
// KEEP_LEAD, applying their writes at once. Latency stays below ~70 ms and
// the final register state is never lost to a full ring.
// The ring is single producer, single consumer. A full ring drops writes,
// never the frame markers.
//
// REGS: register count of all chips, reg = chip * 16 + register.
template <int REGS> class AyLog {
public:
  static const int FRAME_SAMPLES = 400;  // 24 kHz / 60 Hz
  static const int SLICE_SAMPLES = 8;    // replay granularity, 0.33 ms
  static const int MAX_LEAD = 4;         // queued frames that trigger a skip
  static const int KEEP_LEAD = 2;        // queued frames left after a skip

  static_assert(FRAME_SAMPLES % SLICE_SAMPLES == 0, "frame is a whole number of slices");
  static_assert(AUDIO_CHUNK % SLICE_SAMPLES == 0, "chunk is a whole number of slices");
  static_assert(ATOMIC_INT_LOCK_FREE == 2, "AyLog needs lock-free 32-bit atomics");

  // Not while the audio side plays this log.
  void reset(void) {
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
    queued.store(0, std::memory_order_relaxed);
    memset(regs, 0, sizeof(regs));
    cursor = 0;
    step = 0;
    steps = 1;
  }

  // --- emulation core ---

  // steps: loop iterations of run_frame, the unit of at()
  void beginFrame(uint32_t frameSteps) {
    steps = frameSteps;
    step = 0;
  }

  void at(uint32_t now) { step = now; }

  void write(uint8_t reg, uint8_t value) {
    if (used() >= CAPACITY - MARKER_RESERVE) {
      return;
    }
    const uint32_t pos = step * FRAME_SAMPLES / steps;
    push({(uint16_t)std::min<uint32_t>(pos, FRAME_SAMPLES - 1), reg, value});
  }

  void endFrame(void) {
    if (push({0, MARKER, 0})) {
      queued.fetch_add(1, std::memory_order_release);
    }
  }

  // --- audio core ---

  // Fills out[0..n) in slices: render(const uint8_t *regs, int16_t *out, int count)
  template <typename Render> void play(int16_t *out, int n, Render render) {
    for (int i = 0; i < n; i += SLICE_SAMPLES) {
      drain(cursor + SLICE_SAMPLES);
      render(regs, out + i, SLICE_SAMPLES);
      cursor += SLICE_SAMPLES;
      if (cursor >= FRAME_SAMPLES) {
        endOfFrame();
      }
    }
  }

private:
  static const uint8_t MARKER = 0xFF;
  static const uint16_t CAPACITY = 1024;  // power of 2
  static const uint16_t MARKER_RESERVE = 8;

  struct Entry {
    uint16_t pos;
    uint8_t reg;
    uint8_t value;
  };

  uint16_t used(void) const {
    return (tail.load(std::memory_order_relaxed) - head.load(std::memory_order_acquire)) & (CAPACITY - 1);
  }

  bool push(Entry e) {
    const uint16_t t = tail.load(std::memory_order_relaxed);
    const uint16_t next = (t + 1) & (CAPACITY - 1);
    if (next == head.load(std::memory_order_acquire)) {
      return false;
    }
    ring[t] = e;
    tail.store(next, std::memory_order_release);
    return true;
  }

  // true if the head was an end marker, now consumed
  bool popMarker(void) {
    drain(UINT32_MAX);
    const uint16_t h = head.load(std::memory_order_relaxed);
    if (h == tail.load(std::memory_order_acquire) || ring[h].reg != MARKER) {
      return false;
    }
    head.store((h + 1) & (CAPACITY - 1), std::memory_order_release);
    queued.fetch_sub(1, std::memory_order_relaxed);
    return true;
  }

  // applies the writes due before sample `limit` of the current frame
  void drain(uint32_t limit) {
    uint16_t h = head.load(std::memory_order_relaxed);
    while (h != tail.load(std::memory_order_acquire)) {
      const Entry e = ring[h];
      if (e.reg == MARKER || e.pos >= limit) {
        break;
      }
      if (e.reg < REGS) {
        regs[e.reg] = e.value;
      }
      h = (h + 1) & (CAPACITY - 1);
    }
    head.store(h, std::memory_order_release);
  }

  // Next frame once the end marker is there; else wait on the last slice.
  void endOfFrame(void) {
    if (!popMarker()) {
      cursor = FRAME_SAMPLES - SLICE_SAMPLES;
      return;
    }
    cursor = 0;

    // emulation far ahead: catch up by applying whole frames at once
    if (queued.load(std::memory_order_acquire) > MAX_LEAD) {
      while (queued.load(std::memory_order_acquire) > KEEP_LEAD && popMarker()) {
      }
    }
  }

  Entry ring[CAPACITY];
  std::atomic<uint16_t> head{0};  // audio core
  std::atomic<uint16_t> tail{0};  // emulation core
  std::atomic<uint16_t> queued{0};  // frames with an end marker in the ring
  uint8_t regs[REGS] = {};        // audio core: register state being played
  uint32_t cursor = 0;            // audio core: sample inside the frame
  uint32_t step = 0;              // emulation core
  uint32_t steps = 1;
};

#endif
