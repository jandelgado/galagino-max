#ifndef IMA_ADPCM_H
#define IMA_ADPCM_H

#include <atomic>
#include <stdint.h>

// 4-bit IMA-ADPCM decode: one nibble in, predictor+step updated in place.
// Must match romconv/romconv/gfx/adpcm.py's encoder exactly.
namespace ima_adpcm {
  static const int ima_step_table[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
    73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
    408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,
    1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
    5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
    18500,20350,22385,24623,27086,29794,32767};
  static const int8_t ima_index_table[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};

  inline void ima_adpcm_decode(int16_t &predictor, int8_t &step_index, uint8_t code) {
    int step = ima_step_table[step_index], delta = step >> 3;
    if (code & 1) delta += step >> 2;
    if (code & 2) delta += step >> 1;
    if (code & 4) delta += step;
    int value = predictor + ((code & 8) ? -delta : delta);
    if (value > 32767) value = 32767; else if (value < -32768) value = -32768;
    predictor = (int16_t)value;
    int index = step_index + ima_index_table[code];
    step_index = (int8_t)(index < 0 ? 0 : (index > 88 ? 88 : index));
  }

  // One sample stream in romconv/romconv/gfx/adpcm.py format (2-byte predictor,
  // then one nibble per sample), played one decoded sample per next() and
  // held `hold` output samples (2: 12 kHz data at 24 kHz output).
  //
  // start()/stop() may run on the emulation core while the audio core
  // calls next(). start() only publishes the stream and bumps a sequence
  // number; next() owns the decode state and restarts on a new sequence:
  //
  //   emulation core               audio core
  //   start(data, count) --seq--> next(): restart, decode, hold
  class Voice {
  public:
    // count: decoded samples (the asset blob count)
    void start(const uint8_t *data, uint32_t count, uint8_t hold = 1) {
      data_ = data;
      count_ = count;
      hold_ = hold;
      seq_.fetch_add(1, std::memory_order_release);
    }

    void stop() { start(nullptr, 0); }

    // samples left, or a start() the audio core has not picked up yet
    bool active() const {
      return seq_.load(std::memory_order_acquire) != seen_.load(std::memory_order_relaxed) ||
             left_.load(std::memory_order_relaxed) != 0;
    }

    // next output sample on the signed 8-bit scale, 0 when idle
    int16_t next() {
      sync();

      const uint32_t left = left_.load(std::memory_order_relaxed);
      if (!left) {
        return 0;
      }

      if (!repeat_) {
        decode();
        repeat_ = play_hold_;
      }

      const int16_t out = predictor_ >> 8;
      if (!--repeat_) {
        left_.store(left - 1, std::memory_order_relaxed);
      }
      return out;
    }

  private:
    // take over a new start(): copy the stream so a later start() cannot
    // change it mid-decode
    void sync() {
      const uint8_t seq = seq_.load(std::memory_order_acquire);
      if (seq == seen_.load(std::memory_order_relaxed)) {
        return;
      }

      seen_.store(seq, std::memory_order_relaxed);
      play_data_ = data_;
      play_hold_ = hold_ ? hold_ : 1;
      pos_ = 0;
      repeat_ = 0;
      left_.store(play_data_ ? count_ : 0, std::memory_order_relaxed);
    }

    void decode() {
      if (pos_ == 0) {
        predictor_ = (int16_t)(play_data_[0] | (play_data_[1] << 8));
        step_ = 0;
      } else {
        const uint32_t nibble = pos_ - 1;
        const uint8_t packed = play_data_[2 + (nibble >> 1)];
        ima_adpcm_decode(predictor_, step_, (nibble & 1) ? (packed >> 4) : (packed & 0x0f));
      }
      pos_++;
    }

    // written by start()
    const uint8_t *data_ = nullptr;
    uint32_t count_ = 0;
    uint8_t hold_ = 1;
    std::atomic<uint8_t> seq_{0};

    // audio core
    std::atomic<uint8_t> seen_{0};
    std::atomic<uint32_t> left_{0};
    const uint8_t *play_data_ = nullptr;
    uint32_t pos_ = 0;
    int16_t predictor_ = 0;
    int8_t step_ = 0;
    uint8_t play_hold_ = 1;
    uint8_t repeat_ = 0;
  };
}

#endif
