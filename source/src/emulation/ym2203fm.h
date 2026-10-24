#ifndef YM2203FM_H
#define YM2203FM_H

#include <atomic>
#include <stdint.h>

// FM part of a Yamaha YM2203 (OPN): 3 channels of 4 operators. Port of
// ymfm's OPN engine (BSD-3-Clause, Copyright Aaron Giles), integer math
// kept 1:1 so one clock() equals one ymfm FM sample. Dropped: SSG-EG, CSM,
// channel 3 multi-frequency mode, timers and status (1943, commando and
// gng never use them). The SSG part goes through the AY renderer.
//
//   emulation core (sound CPU)        audio core
//   writeAddress/writeData --> ring --> render(): apply writes, then
//   bursts per sound IRQ               FM ticks at fmRate(), one output
//   writeMark() after each slice       sample per out_rate tick
//
// Without marks render() applies every queued write. With marks it applies
// the writes up to the next mark only: one mark per render() call keeps
// the sound CPU's timing inside a frame (marble: 6 slices, 6 calls).
class Ym2203Fm {
public:
  static const uint16_t RING_SIZE = 256;

  // chip clock in Hz (1943, commando, gng: 1.5 MHz). average: each output
  // sample is the mean of the FM ticks it covers, for fmRate() above the
  // output rate (no aliasing, feedback at the native rate; one mix() per
  // tick instead of per sample)
  explicit Ym2203Fm(uint32_t clock = 1500000, bool average = false)
    : clock_hz(clock), average_(average) {}

  // both cores stopped
  void reset();

  // emulation core: chip address / data port writes. Only the prescaler
  // addresses ($2D-$2F) and FM registers ($10+) are queued.
  void writeAddress(uint8_t addr);
  void writeData(uint8_t reg, uint8_t value);
  // emulation core: the writes so far belong to the next render() call
  void writeMark();
  uint16_t dropped() const { return dropped_; } // queue overflows

  // audio core: adds n samples at out_rate onto out
  void render(int32_t *out, int n, uint32_t out_rate);

  // host tests: apply a write now
  void applyAddress(uint8_t addr);
  void applyWrite(uint8_t reg, uint8_t value);

  // one native FM sample (ymfm ym2203::clock_fm)
  int16_t clock();
  uint32_t fmRate() const;

private:
  static const uint8_t CHANNELS = 3;
  static const uint8_t OPS = 4;

  enum EgState : uint8_t { EG_ATTACK = 1, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

  struct Op {
    uint8_t offset; // register offset: reg 0x30 + offset etc.
    uint32_t phase; // 10.10
    uint16_t env;   // attenuation 0..0x3ff
    uint8_t state;
    uint8_t key_state;
    uint8_t keyon_live;
    // cached from the registers in prepare()
    uint32_t phase_step;
    uint16_t total_level;
    uint16_t sustain;
    uint8_t rate[EG_RELEASE + 1];
  };

  struct Channel {
    int16_t feedback[2];
    int16_t feedback_in;
    Op op[OPS];
  };

  uint32_t clock_hz;
  bool average_;
  uint8_t regs[256];
  Channel ch[CHANNELS];
  uint32_t env_counter;
  uint8_t prescale;
  uint8_t active;   // channel bit set: some operator still sounds
  bool modified;    // a register changed since the last prepare()
  uint16_t prepare_count;

  // single producer, single consumer; one slot stays empty. Entry:
  // reg << 8 | value; reg < $10 encodes an address write of reg + $20,
  // MARK (reg 1, never an address write) a writeMark().
  uint16_t ring[RING_SIZE];
  std::atomic<uint16_t> head; // producer
  std::atomic<uint16_t> tail; // consumer
  std::atomic<uint16_t> marks_in;  // producer: marks pushed
  uint16_t marks_out;              // consumer: marks applied
  uint16_t dropped_;
  uint32_t tick_frac; // 16.16 FM ticks owed to the output
  int16_t last;       // FM sample held between ticks

  void push(uint16_t entry);
  void drain();
  void advance(uint32_t ticks);
  int16_t mix();
  uint8_t reg(uint8_t base, const Op &o) const { return regs[base + o.offset]; }
  void prepare();
  void cacheOp(uint8_t c, Op &o);
  void keystate(Op &o, uint8_t on);
  void envelope(Op &o, uint32_t counter);
  int32_t volume(const Op &o, uint32_t phase) const;
  int32_t output(Channel &c);
};

#endif
