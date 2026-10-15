#ifndef YM2203FM_H
#define YM2203FM_H

#include <stdint.h>

// FM part of a Yamaha YM2203 (OPN): 3 channels of 4 operators. Port of
// ymfm's OPN engine (BSD-3-Clause, Copyright Aaron Giles), integer math
// kept 1:1 so one clock() equals one ymfm FM sample. Dropped: SSG-EG, CSM,
// channel 3 multi-frequency mode, timers and status (1943, commando and
// gng never use them). The SSG part goes through the AY renderer.
class Ym2203Fm {
public:
  static const uint32_t CLOCK = 1500000; // 1943, commando, gng

  void reset();

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

  uint8_t regs[256];
  Channel ch[CHANNELS];
  uint32_t env_counter;
  uint8_t prescale;
  uint8_t active;   // channel bit set: some operator still sounds
  bool modified;    // a register changed since the last prepare()
  uint16_t prepare_count;

  uint8_t reg(uint8_t base, const Op &o) const { return regs[base + o.offset]; }
  void prepare();
  void cacheOp(uint8_t c, Op &o);
  void keystate(Op &o, uint8_t on);
  void envelope(Op &o, uint32_t counter);
  int32_t volume(const Op &o, uint32_t phase) const;
  int32_t output(Channel &c);
};

#endif
