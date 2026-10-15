#include "ym2203fm.h"

#include <string.h>

// Port of ymfm (https://github.com/aaronsgiles/ymfm), BSD-3-Clause,
// Copyright (c) 2021, Aaron Giles. All rights reserved. Function names in
// the comments refer to ymfm_fm.ipp / ymfm_opn.cpp.

static const uint16_t EG_QUIET = 0x380;
static const uint16_t EG_MAX = 0x3ff;
static const uint16_t PREPARE_PERIOD = 4096; // re-prepare to catch ending notes
static const uint8_t REG_KEY = 0x28;
static const uint8_t REG_MODE = 0x27;
static const uint8_t KEY_CHANNEL_NONE = 3;
static const uint8_t KEYON_NORMAL = 1; // keyon_live bit 0

// abs_sin_attenuation: 4.8 log attenuation of 1/4 sine, from the die
static const uint16_t SIN_TABLE[256] = {
  0x859,0x6c3,0x607,0x58b,0x52e,0x4e4,0x4a6,0x471,0x443,0x41a,0x3f5,0x3d3,0x3b5,0x398,0x37e,0x365,
  0x34e,0x339,0x324,0x311,0x2ff,0x2ed,0x2dc,0x2cd,0x2bd,0x2af,0x2a0,0x293,0x286,0x279,0x26d,0x261,
  0x256,0x24b,0x240,0x236,0x22c,0x222,0x218,0x20f,0x206,0x1fd,0x1f5,0x1ec,0x1e4,0x1dc,0x1d4,0x1cd,
  0x1c5,0x1be,0x1b7,0x1b0,0x1a9,0x1a2,0x19b,0x195,0x18f,0x188,0x182,0x17c,0x177,0x171,0x16b,0x166,
  0x160,0x15b,0x155,0x150,0x14b,0x146,0x141,0x13c,0x137,0x133,0x12e,0x129,0x125,0x121,0x11c,0x118,
  0x114,0x10f,0x10b,0x107,0x103,0x0ff,0x0fb,0x0f8,0x0f4,0x0f0,0x0ec,0x0e9,0x0e5,0x0e2,0x0de,0x0db,
  0x0d7,0x0d4,0x0d1,0x0cd,0x0ca,0x0c7,0x0c4,0x0c1,0x0be,0x0bb,0x0b8,0x0b5,0x0b2,0x0af,0x0ac,0x0a9,
  0x0a7,0x0a4,0x0a1,0x09f,0x09c,0x099,0x097,0x094,0x092,0x08f,0x08d,0x08a,0x088,0x086,0x083,0x081,
  0x07f,0x07d,0x07a,0x078,0x076,0x074,0x072,0x070,0x06e,0x06c,0x06a,0x068,0x066,0x064,0x062,0x060,
  0x05e,0x05c,0x05b,0x059,0x057,0x055,0x053,0x052,0x050,0x04e,0x04d,0x04b,0x04a,0x048,0x046,0x045,
  0x043,0x042,0x040,0x03f,0x03e,0x03c,0x03b,0x039,0x038,0x037,0x035,0x034,0x033,0x031,0x030,0x02f,
  0x02e,0x02d,0x02b,0x02a,0x029,0x028,0x027,0x026,0x025,0x024,0x023,0x022,0x021,0x020,0x01f,0x01e,
  0x01d,0x01c,0x01b,0x01a,0x019,0x018,0x017,0x017,0x016,0x015,0x014,0x014,0x013,0x012,0x011,0x011,
  0x010,0x00f,0x00f,0x00e,0x00d,0x00d,0x00c,0x00c,0x00b,0x00a,0x00a,0x009,0x009,0x008,0x008,0x007,
  0x007,0x007,0x006,0x006,0x005,0x005,0x005,0x004,0x004,0x004,0x003,0x003,0x003,0x002,0x002,0x002,
  0x002,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000
};

// attenuation_to_volume: 10-bit mantissas, implicit 0x400 bit, << 2, reversed
#define X(a) (((a) | 0x400) << 2)
static const uint16_t POWER_TABLE[256] = {
  X(0x3fa),X(0x3f5),X(0x3ef),X(0x3ea),X(0x3e4),X(0x3df),X(0x3da),X(0x3d4),
  X(0x3cf),X(0x3c9),X(0x3c4),X(0x3bf),X(0x3b9),X(0x3b4),X(0x3ae),X(0x3a9),
  X(0x3a4),X(0x39f),X(0x399),X(0x394),X(0x38f),X(0x38a),X(0x384),X(0x37f),
  X(0x37a),X(0x375),X(0x370),X(0x36a),X(0x365),X(0x360),X(0x35b),X(0x356),
  X(0x351),X(0x34c),X(0x347),X(0x342),X(0x33d),X(0x338),X(0x333),X(0x32e),
  X(0x329),X(0x324),X(0x31f),X(0x31a),X(0x315),X(0x310),X(0x30b),X(0x306),
  X(0x302),X(0x2fd),X(0x2f8),X(0x2f3),X(0x2ee),X(0x2e9),X(0x2e5),X(0x2e0),
  X(0x2db),X(0x2d6),X(0x2d2),X(0x2cd),X(0x2c8),X(0x2c4),X(0x2bf),X(0x2ba),
  X(0x2b5),X(0x2b1),X(0x2ac),X(0x2a8),X(0x2a3),X(0x29e),X(0x29a),X(0x295),
  X(0x291),X(0x28c),X(0x288),X(0x283),X(0x27f),X(0x27a),X(0x276),X(0x271),
  X(0x26d),X(0x268),X(0x264),X(0x25f),X(0x25b),X(0x257),X(0x252),X(0x24e),
  X(0x249),X(0x245),X(0x241),X(0x23c),X(0x238),X(0x234),X(0x230),X(0x22b),
  X(0x227),X(0x223),X(0x21e),X(0x21a),X(0x216),X(0x212),X(0x20e),X(0x209),
  X(0x205),X(0x201),X(0x1fd),X(0x1f9),X(0x1f5),X(0x1f0),X(0x1ec),X(0x1e8),
  X(0x1e4),X(0x1e0),X(0x1dc),X(0x1d8),X(0x1d4),X(0x1d0),X(0x1cc),X(0x1c8),
  X(0x1c4),X(0x1c0),X(0x1bc),X(0x1b8),X(0x1b4),X(0x1b0),X(0x1ac),X(0x1a8),
  X(0x1a4),X(0x1a0),X(0x19c),X(0x199),X(0x195),X(0x191),X(0x18d),X(0x189),
  X(0x185),X(0x181),X(0x17e),X(0x17a),X(0x176),X(0x172),X(0x16f),X(0x16b),
  X(0x167),X(0x163),X(0x160),X(0x15c),X(0x158),X(0x154),X(0x151),X(0x14d),
  X(0x149),X(0x146),X(0x142),X(0x13e),X(0x13b),X(0x137),X(0x134),X(0x130),
  X(0x12c),X(0x129),X(0x125),X(0x122),X(0x11e),X(0x11b),X(0x117),X(0x114),
  X(0x110),X(0x10c),X(0x109),X(0x106),X(0x102),X(0x0ff),X(0x0fb),X(0x0f8),
  X(0x0f4),X(0x0f1),X(0x0ed),X(0x0ea),X(0x0e7),X(0x0e3),X(0x0e0),X(0x0dc),
  X(0x0d9),X(0x0d6),X(0x0d2),X(0x0cf),X(0x0cc),X(0x0c8),X(0x0c5),X(0x0c2),
  X(0x0be),X(0x0bb),X(0x0b8),X(0x0b5),X(0x0b1),X(0x0ae),X(0x0ab),X(0x0a8),
  X(0x0a4),X(0x0a1),X(0x09e),X(0x09b),X(0x098),X(0x094),X(0x091),X(0x08e),
  X(0x08b),X(0x088),X(0x085),X(0x082),X(0x07e),X(0x07b),X(0x078),X(0x075),
  X(0x072),X(0x06f),X(0x06c),X(0x069),X(0x066),X(0x063),X(0x060),X(0x05d),
  X(0x05a),X(0x057),X(0x054),X(0x051),X(0x04e),X(0x04b),X(0x048),X(0x045),
  X(0x042),X(0x03f),X(0x03c),X(0x039),X(0x036),X(0x033),X(0x030),X(0x02d),
  X(0x02a),X(0x028),X(0x025),X(0x022),X(0x01f),X(0x01c),X(0x019),X(0x016),
  X(0x014),X(0x011),X(0x00e),X(0x00b),X(0x008),X(0x006),X(0x003),X(0x000)
};
#undef X

// attenuation_increment: 4-bit steps per 6-bit rate and 3-bit index
static const uint32_t INCREMENT_TABLE[64] = {
  0x00000000, 0x00000000, 0x10101010, 0x10101010, 0x10101010, 0x10101010, 0x11101110, 0x11101110,
  0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
  0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
  0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
  0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
  0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
  0x11111111, 0x21112111, 0x21212121, 0x22212221, 0x22222222, 0x42224222, 0x42424242, 0x44424442,
  0x44444444, 0x84448444, 0x84848484, 0x88848884, 0x88888888, 0x88888888, 0x88888888, 0x88888888
};

// detune_adjustment per 5-bit keycode and detune & 3
static const uint8_t DETUNE_TABLE[32][4] = {
  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },
  { 0,  1,  2,  2 },  { 0,  1,  2,  3 },  { 0,  1,  2,  3 },  { 0,  1,  2,  3 },
  { 0,  1,  2,  4 },  { 0,  1,  3,  4 },  { 0,  1,  3,  4 },  { 0,  1,  3,  5 },
  { 0,  2,  4,  5 },  { 0,  2,  4,  6 },  { 0,  2,  4,  6 },  { 0,  2,  5,  7 },
  { 0,  2,  5,  8 },  { 0,  3,  6,  8 },  { 0,  3,  6,  9 },  { 0,  3,  7, 10 },
  { 0,  4,  8, 11 },  { 0,  4,  8, 12 },  { 0,  4,  9, 13 },  { 0,  5, 10, 14 },
  { 0,  5, 11, 16 },  { 0,  6, 12, 17 },  { 0,  6, 13, 19 },  { 0,  7, 14, 20 },
  { 0,  8, 16, 22 },  { 0,  8, 16, 22 },  { 0,  8, 16, 22 },  { 0,  8, 16, 22 }
};

// s_algorithm_ops: op2/op3/op4 modulation input (opout index) and which
// of op1..op3 also reach the output
#define ALGORITHM(op2in, op3in, op4in, op1out, op2out, op3out) \
  ((op2in) | ((op3in) << 1) | ((op4in) << 4) | ((op1out) << 7) | ((op2out) << 8) | ((op3out) << 9))
static const uint16_t ALGORITHM_OPS[8] = {
  ALGORITHM(1,2,3, 0,0,0), // 0: O1 -> O2 -> O3 -> O4 -> out (O4)
  ALGORITHM(0,5,3, 0,0,0), // 1: (O1 + O2) -> O3 -> O4 -> out (O4)
  ALGORITHM(0,2,6, 0,0,0), // 2: (O1 + (O2 -> O3)) -> O4 -> out (O4)
  ALGORITHM(1,0,7, 0,0,0), // 3: ((O1 -> O2) + O3) -> O4 -> out (O4)
  ALGORITHM(1,0,3, 0,1,0), // 4: ((O1 -> O2) + (O3 -> O4)) -> out (O2+O4)
  ALGORITHM(1,1,1, 0,1,1), // 5: ((O1 -> O2) + (O1 -> O3) + (O1 -> O4)) -> out (O2+O3+O4)
  ALGORITHM(1,0,0, 0,1,1), // 6: ((O1 -> O2) + O3 + O4) -> out (O2+O3+O4)
  ALGORITHM(0,0,0, 1,1,1)  // 7: (O1 + O2 + O3 + O4) -> out (O1+O2+O3+O4)
};
#undef ALGORITHM

// operator register offsets inside a channel: carrier 1, modulator 1,
// carrier 2, modulator 2 (ymfm opn operator_map, bit-swapped)
static const uint8_t OP_OFFSET[4] = {0, 8, 4, 12};

static inline uint32_t bits(uint32_t v, int start, int count) {
  return (v >> start) & ((1u << count) - 1);
}

static inline int32_t clampOut(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// effective_rate: KSR added unless the raw rate is 0, max 63
static inline uint8_t effectiveRate(uint32_t raw, uint32_t ksr) {
  if (raw == 0) {
    return 0;
  }
  return raw + ksr > 63 ? 63 : raw + ksr;
}

// roundtrip_fp: through the 10.3 floating point DAC and back
static int16_t roundtripFp(int32_t value) {
  if (value < -32768) {
    return -32768;
  }
  if (value > 32767) {
    return 32767;
  }
  const uint32_t scan = (uint32_t)(value ^ (value >> 31)) << 17;
  const int clz = scan == 0 ? 32 : __builtin_clz(scan);
  int exponent = 7 - clz;
  if (exponent < 1) {
    exponent = 1;
  }
  exponent -= 1;
  return (int16_t)((value >> exponent) << exponent);
}

void Ym2203Fm::reset() {
  memset(regs, 0, sizeof(regs));
  memset(ch, 0, sizeof(ch));
  for (uint8_t c = 0; c < CHANNELS; c++) {
    for (uint8_t n = 0; n < OPS; n++) {
      Op &o = ch[c].op[n];
      o.offset = c + OP_OFFSET[n];
      o.env = EG_MAX;
      o.state = EG_RELEASE;
    }
  }
  env_counter = 0;
  prescale = 6;
  active = (1 << CHANNELS) - 1;
  modified = true;
  prepare_count = 0;

  head.store(0, std::memory_order_relaxed);
  tail.store(0, std::memory_order_relaxed);
  dropped_ = 0;
  tick_frac = 0;
  last = 0;
}

void Ym2203Fm::push(uint16_t entry) {
  const uint16_t h = head.load(std::memory_order_relaxed);
  const uint16_t next = (h + 1) % RING_SIZE;
  if (next == tail.load(std::memory_order_acquire)) {
    dropped_++;
    return;
  }
  ring[h] = entry;
  head.store(next, std::memory_order_release);
}

void Ym2203Fm::writeAddress(uint8_t addr) {
  if (addr >= 0x2d && addr <= 0x2f) {
    push((uint16_t)(addr - 0x20) << 8);
  }
}

void Ym2203Fm::writeData(uint8_t reg, uint8_t value) {
  if (reg >= 0x10) {
    push((uint16_t)(reg << 8) | value);
  }
}

// Applies the queued writes. Key writes of one batch were milliseconds
// apart on the sound CPU but land between two FM ticks here; prepare()
// in between keeps a key off + key on pair from collapsing into "on".
void Ym2203Fm::drain() {
  uint16_t t = tail.load(std::memory_order_relaxed);
  const uint16_t h = head.load(std::memory_order_acquire);
  bool key_pending = false;
  for (; t != h; t = (t + 1) % RING_SIZE) {
    const uint8_t reg = ring[t] >> 8;
    if (reg < 0x10) {
      applyAddress(reg + 0x20);
      continue;
    }
    if (reg == REG_KEY) {
      if (key_pending) {
        prepare();
      }
      key_pending = true;
    }
    applyWrite(reg, ring[t] & 0xff);
  }
  tail.store(t, std::memory_order_release);
}

// FM ticks run at fmRate(); the output takes the latest tick's sample.
// At out_rate == fmRate() this is exactly clock() per sample.
void Ym2203Fm::render(int32_t *out, int n, uint32_t out_rate) {
  drain();
  const uint32_t step = (uint32_t)(((uint64_t)fmRate() << 16) / out_rate);
  for (int i = 0; i < n; i++) {
    tick_frac += step;
    const uint32_t ticks = tick_frac >> 16;
    tick_frac &= 0xffff;
    if (ticks) {
      advance(ticks);
      last = mix();
    }
    out[i] += last;
  }
}

uint32_t Ym2203Fm::fmRate() const {
  return CLOCK / (12 * prescale);
}

// ym2203::write_address: $2D-$2F select the prescaler
void Ym2203Fm::applyAddress(uint8_t addr) {
  if (addr == 0x2d) {
    prescale = 6;
  } else if (addr == 0x2e && prescale == 6) {
    prescale = 3;
  } else if (addr == 0x2f) {
    prescale = 2;
  }
}

// fm_engine_base::write + opn_registers_base::write
void Ym2203Fm::applyWrite(uint8_t reg, uint8_t value) {
  if (reg < 0x10) {
    return; // SSG
  }
  modified = true;
  if (reg == REG_MODE) {
    regs[reg] = value; // timers and ch3 modes not emulated
    return;
  }

  // $A0-$A6: upper half latches, lower half commits both
  if ((reg & 0xf0) == 0xa0) {
    if (bits(reg, 0, 2) == 3) {
      return;
    }
    const uint8_t latch = 0xb8 | bits(reg, 3, 1);
    if (bits(reg, 2, 1)) {
      regs[latch] = value | 0x80;
    } else if (regs[latch] & 0x80) {
      regs[reg] = value;
      regs[reg | 4] = regs[latch] & 0x3f;
      regs[latch] = 0;
    }
    return;
  }
  if ((reg & 0xf8) == 0xb8) {
    return; // internal latches
  }

  regs[reg] = value;
  if (reg != REG_KEY) {
    return;
  }
  const uint8_t c = bits(value, 0, 2);
  if (c == KEY_CHANNEL_NONE) {
    return;
  }
  for (uint8_t n = 0; n < OPS; n++) {
    Op &o = ch[c].op[n];
    o.keyon_live = (o.keyon_live & ~KEYON_NORMAL) | bits(value, 4 + n, 1);
  }
}

// cache_operator_data + compute_phase_step (no LFO)
void Ym2203Fm::cacheOp(uint8_t c, Op &o) {
  const uint32_t block_freq = (bits(regs[0xa4 + c], 0, 6) << 8) | regs[0xa0 + c];

  uint32_t keycode = bits(block_freq, 10, 4) << 1;
  keycode |= bits(0xfe80, bits(block_freq, 7, 4), 1);

  const uint8_t r30 = reg(0x30, o);
  const uint32_t detune = bits(r30, 4, 3);
  int32_t dt = DETUNE_TABLE[keycode][detune & 3];
  if (bits(detune, 2, 1)) {
    dt = -dt;
  }
  uint32_t multiple = bits(r30, 0, 4) * 2;
  if (multiple == 0) {
    multiple = 1;
  }

  const uint32_t fnum = bits(block_freq, 0, 11) << 1;
  const uint32_t block = bits(block_freq, 11, 3);
  uint32_t step = (fnum << block) >> 2;
  step += dt;
  step &= 0x1ffff;
  o.phase_step = (step * multiple) >> 1;

  o.total_level = bits(reg(0x40, o), 0, 7) << 3;

  // 4-bit sustain level, 15 means 31
  uint32_t sl = bits(reg(0x80, o), 4, 4);
  sl |= (sl + 1) & 0x10;
  o.sustain = sl << 5;

  const uint8_t r50 = reg(0x50, o);
  const uint32_t ksr = keycode >> (bits(r50, 6, 2) ^ 3);
  o.rate[EG_ATTACK] = effectiveRate(bits(r50, 0, 5) * 2, ksr);
  o.rate[EG_DECAY] = effectiveRate(bits(reg(0x60, o), 0, 5) * 2, ksr);
  o.rate[EG_SUSTAIN] = effectiveRate(bits(reg(0x70, o), 0, 5) * 2, ksr);
  o.rate[EG_RELEASE] = effectiveRate(bits(reg(0x80, o), 0, 4) * 4 + 2, ksr);
}

// clock_keystate, start_attack, start_release
void Ym2203Fm::keystate(Op &o, uint8_t on) {
  if (on == o.key_state) {
    return;
  }
  o.key_state = on;
  if (!on) {
    if (o.state < EG_RELEASE) {
      o.state = EG_RELEASE;
    }
    return;
  }
  if (o.state == EG_ATTACK) {
    return;
  }
  o.state = EG_ATTACK;
  o.phase = 0;
  if (o.rate[EG_ATTACK] >= 62) {
    o.env = 0;
  }
}

// fm_channel::prepare for all channels: cache registers, apply key changes
void Ym2203Fm::prepare() {
  active = 0;
  for (uint8_t c = 0; c < CHANNELS; c++) {
    for (uint8_t n = 0; n < OPS; n++) {
      Op &o = ch[c].op[n];
      cacheOp(c, o);
      keystate(o, o.keyon_live != 0);
      if (o.state != EG_RELEASE || o.env < EG_QUIET) {
        active |= 1 << c;
      }
    }
  }
}

// clock_envelope; counter = env_counter >> 2
void Ym2203Fm::envelope(Op &o, uint32_t counter) {
  if (o.state == EG_ATTACK && o.env == 0) {
    o.state = EG_DECAY;
  }
  if (o.state == EG_DECAY && o.env >= o.sustain) {
    o.state = EG_SUSTAIN;
  }

  const uint32_t rate = o.rate[o.state];
  const uint32_t shift = rate >> 2;
  counter <<= shift;
  if (bits(counter, 0, 11) != 0) {
    return;
  }
  const uint32_t index = bits(counter, shift <= 11 ? 11 : shift, 3);
  const uint32_t inc = bits(INCREMENT_TABLE[rate], 4 * index, 4);

  if (o.state == EG_ATTACK) {
    if (rate < 62) {
      o.env += (~o.env * inc) >> 4;
    }
    return;
  }
  o.env += inc;
  if (o.env >= 0x400) {
    o.env = EG_MAX;
  }
}

// compute_volume without AM: 14-bit signed operator output
int32_t Ym2203Fm::volume(const Op &o, uint32_t phase) const {
  if (o.env > EG_QUIET) {
    return 0;
  }
  // sine attenuation, mirrored in the second quarter, negative in the second half
  uint32_t in = phase & 0x3ff;
  const bool negative = bits(in, 9, 1);
  if (bits(in, 8, 1)) {
    in = ~in;
  }
  const uint32_t sin_att = SIN_TABLE[in & 0xff];

  uint32_t env = o.env + o.total_level;
  if (env > EG_MAX) {
    env = EG_MAX;
  }
  const uint32_t att = sin_att + (env << 2);
  const int32_t result = POWER_TABLE[att & 0xff] >> (att >> 8);
  return negative ? -result : result;
}

// fm_channel::output_4op, rshift 0, clipmax 32767
int32_t Ym2203Fm::output(Channel &c) {
  const uint8_t b0 = regs[0xb0 + (c.op[0].offset & 3)];
  const uint32_t feedback = bits(b0, 3, 3);
  int32_t opmod = 0;
  if (feedback != 0) {
    opmod = (c.feedback[0] + c.feedback[1]) >> (10 - feedback);
  }

  int16_t opout[8];
  opout[0] = 0;
  opout[1] = c.feedback_in = volume(c.op[0], (c.op[0].phase >> 10) + opmod);

  const uint32_t algo = ALGORITHM_OPS[bits(b0, 0, 3)];
  opmod = opout[bits(algo, 0, 1)] >> 1;
  opout[2] = volume(c.op[1], (c.op[1].phase >> 10) + opmod);
  opout[5] = opout[1] + opout[2];

  opmod = opout[bits(algo, 1, 3)] >> 1;
  opout[3] = volume(c.op[2], (c.op[2].phase >> 10) + opmod);
  opout[6] = opout[1] + opout[3];
  opout[7] = opout[2] + opout[3];

  opmod = opout[bits(algo, 4, 3)] >> 1;
  int32_t result = volume(c.op[3], (c.op[3].phase >> 10) + opmod);

  const int32_t clipmax = 32767;
  const int32_t clipmin = -clipmax - 1;
  if (bits(algo, 7, 1)) {
    result = clampOut(result + opout[1], clipmin, clipmax);
  }
  if (bits(algo, 8, 1)) {
    result = clampOut(result + opout[2], clipmin, clipmax);
  }
  if (bits(algo, 9, 1)) {
    result = clampOut(result + opout[3], clipmin, clipmax);
  }
  return result;
}

// ym2203::clock_fm: engine clock, output of the active channels, DAC
int16_t Ym2203Fm::clock() {
  advance(1);
  return mix();
}

// fm_engine_base::clock, ticks times in a row. Only the envelopes need
// every tick; prepare() changes phases only on the first tick (writes are
// applied before render's loop), so phases advance once by ticks steps.
void Ym2203Fm::advance(uint32_t ticks) {
  for (uint32_t t = 0; t < ticks; t++) {
    if (modified || prepare_count++ >= PREPARE_PERIOD) {
      prepare();
      modified = false;
      prepare_count = 0;
    }

    // envelope counter x.2, the sub-counter wraps at 3
    if (bits(++env_counter, 0, 2) == 3) {
      env_counter += 1;
    }
    if (bits(env_counter, 0, 2) != 0) {
      continue;
    }
    for (uint8_t c = 0; c < CHANNELS; c++) {
      for (uint8_t n = 0; n < OPS; n++) {
        envelope(ch[c].op[n], env_counter >> 2);
      }
    }
  }

  for (uint8_t c = 0; c < CHANNELS; c++) {
    Channel &chan = ch[c];
    chan.feedback[0] = ticks > 1 ? chan.feedback_in : chan.feedback[1];
    chan.feedback[1] = chan.feedback_in;
    for (uint8_t n = 0; n < OPS; n++) {
      chan.op[n].phase += chan.op[n].phase_step * ticks;
    }
  }
}

// fm_engine_base::output + roundtrip_fp
int16_t Ym2203Fm::mix() {
  int32_t sum = 0;
  for (uint8_t c = 0; c < CHANNELS; c++) {
    if (active & (1 << c)) {
      sum += output(ch[c]);
    }
  }
  return roundtripFp(sum);
}
