#include "sid.h"
#include <string.h>

// Port of sid_synth_render() / sidPoke() from Rockbox apps/codecs/sid.c
// (TinySID). Digi playback and the 6510 are not part of this file.

// envelope step per sample at 24 kHz: 0x1000000 / (seconds * 24000),
// times from TinySID's attackTimes / decayReleaseTimes
static const int32_t ATTACKS[16] = {
  310294, 87272, 44328, 29397, 18743, 12694, 10459, 8922,
  7124, 2858, 1429, 893, 714, 238, 142, 89};
static const int32_t RELEASES[16] = {
  78388, 28423, 14437, 9574, 6104, 4134, 3406, 2906,
  2320, 931, 465, 290, 232, 77, 46, 29};

// 28 bit phase step per frequency register unit and sample:
// 2^28 * 985248 Hz (PAL) / 2^24 / 24000 = 656.8, NTSC 1022727 Hz: 681.8.
// TinySID's 15872000 / rate is 0.7% sharp.
static const uint32_t FREQ_MUL_PAL = 657;
static const uint32_t FREQ_MUL_NTSC = 682;
static const uint32_t PHASE_MASK = 0xfffffff;
static const uint32_t PHASE_MSB = 0x8000000;

static const int32_t ONE = 1 << 16;       // 1.0 in 16.16
static const int32_t FILT_MUL = 58;       // (21.5332031 << 16) / 24000
static const int32_t REZ_BASE = 78643;    // 1.2 in 16.16
static const int32_t REZ_STEP = 2621;     // 0.04 in 16.16
static const int32_t ENV_MAX = 0xffffff;
static const int32_t ENV_FLOOR = 0x40000; // a real SID never fully silences a voice
static const uint32_t NOISE_SEED = 0x7fffff;

// output: 3 voices x +-511 x volume 15 = +-23000, x 85 >> 12 = +-480
static const int32_t OUT_MUL = 85;
static const int OUT_SHIFT = 12;

// control register bits
static const uint8_t CTRL_GATE = 0x01, CTRL_SYNC = 0x02, CTRL_RING = 0x04, CTRL_TEST = 0x08;
static const uint8_t WAVE_TRI = 0x10, WAVE_SAW = 0x20, WAVE_PULSE = 0x40, WAVE_NOISE = 0x80;
static const uint8_t WAVES = WAVE_TRI | WAVE_SAW | WAVE_PULSE | WAVE_NOISE;

// $D418 bits
static const uint8_t LOWPASS = 0x10, BANDPASS = 0x20, HIGHPASS = 0x40, VOICE3_OFF = 0x80;

enum EnvPhase : uint8_t { ATTACK, DECAY, SUSTAIN, RELEASE };

static const uint8_t VOICE_REGS = 7;
static const uint8_t REG_FC_LO = 0x15, REG_FC_HI = 0x16, REG_RES_FILT = 0x17, REG_MODE_VOL = 0x18;

static inline uint32_t bit(uint32_t v, uint8_t b) { return (v >> b) & 1; }

// 16.16 multiply, both operands pre-shifted to keep 32 bits
static inline int32_t fmul(int32_t a, int32_t b) { return (a >> 8) * (b >> 8); }

void Sid::reset(SidClock clock) {
  freq_mul = clock == SID_NTSC ? FREQ_MUL_NTSC : FREQ_MUL_PAL;
  memset(voice, 0, sizeof(voice));
  memset(osc, 0, sizeof(osc));
  for (int v = 0; v < 3; v++) {
    // TinySID starts in attack: idle voices would ramp up as DC
    osc[v].envphase = RELEASE;
    osc[v].noiseval = NOISE_SEED;
  }
  fc_lo = fc_hi = res_filt = mode_vol = 0;
  flt_h = flt_b = flt_l = 0;
}

void Sid::write(uint8_t reg, uint8_t val) {
  if (reg >= REG_FC_LO) {
    switch (reg) {
      case REG_FC_LO: fc_lo = val; break;
      case REG_FC_HI: fc_hi = val; break;
      case REG_RES_FILT: res_filt = val; break;
      case REG_MODE_VOL: mode_vol = val; break;
    }
    return;
  }

  const uint8_t v = reg / VOICE_REGS;
  Voice &vc = voice[v];
  switch (reg % VOICE_REGS) {
    case 0: vc.freq = (vc.freq & 0xff00) | val; break;
    case 1: vc.freq = (vc.freq & 0x00ff) | (val << 8); break;
    case 2: vc.pulse = (vc.pulse & 0xff00) | val; break;
    case 3: vc.pulse = (vc.pulse & 0x00ff) | (val << 8); break;
    case 4:
      // gate edges act at once: a player may toggle it twice per call
      vc.ctrl = val;
      if (!(val & CTRL_GATE)) {
        osc[v].envphase = RELEASE;
      } else if (osc[v].envphase == RELEASE) {
        osc[v].envphase = ATTACK;
      }
      break;
    case 5: vc.ad = val; break;
    case 6: vc.sr = val; break;
  }
}

void Sid::render(int16_t *out, int n) {
  // registers -> per call constants (they only change between play calls)
  uint32_t freq[3], pulse[3];
  int32_t attack[3], decay[3], sustain[3], release[3];
  for (int v = 0; v < 3; v++) {
    freq[v] = voice[v].freq * freq_mul;
    pulse[v] = (uint32_t)(voice[v].pulse & 0xfff) << 16;
    attack[v] = ATTACKS[voice[v].ad >> 4];
    decay[v] = RELEASES[voice[v].ad & 0xf];
    sustain[v] = (int32_t)(voice[v].sr & 0xf0) << 16;
    release[v] = RELEASES[voice[v].sr & 0xf];
  }

  int32_t fc = (16 * fc_hi + (fc_lo & 7)) * FILT_MUL;
  if (fc > ONE) {
    fc = ONE;
  }
  const int32_t rez = (REZ_BASE - REZ_STEP * (res_filt >> 4)) >> 8;
  const int32_t vol = mode_vol & 0xf;
  const bool voice3 = !(mode_vol & VOICE3_OFF);

  for (int i = 0; i < n; i++) {
    int32_t dry = 0, wet = 0;

    for (int v = 0; v < 3; v++) {
      Osc &o = osc[v];
      const uint8_t ctrl = voice[v].ctrl;
      const int ref = v ? v - 1 : 2; // sync / ring source

      o.counter = (o.counter + freq[v]) & PHASE_MASK;
      if (ctrl & CTRL_TEST) {
        o.counter = 0;
        o.noisepos = 0;
        o.noiseval = NOISE_SEED;
      }
      if ((ctrl & CTRL_SYNC) && osc[ref].counter < freq[ref]) {
        // 64 bit: 28 x 28 bit product (TinySID overflows here)
        o.counter = (uint32_t)((uint64_t)osc[ref].counter * freq[v] / freq[ref]);
      }

      uint8_t tri = (uint8_t)(o.counter >> 19);
      if (o.counter >> 27) {
        tri ^= 0xff;
      }
      if ((ctrl & CTRL_RING) && osc[ref].counter >= PHASE_MSB) {
        tri ^= 0xff;
      }
      const uint8_t saw = (uint8_t)(o.counter >> 20);
      const uint8_t pls = (uint8_t)((o.counter > pulse[v]) - 1);

      if (o.noisepos != (o.counter >> 23)) {
        o.noisepos = o.counter >> 23;
        o.noiseval = (o.noiseval << 1) | (bit(o.noiseval, 22) ^ bit(o.noiseval, 17));
        o.noiseout = (bit(o.noiseval, 22) << 7) | (bit(o.noiseval, 20) << 6) |
                     (bit(o.noiseval, 16) << 5) | (bit(o.noiseval, 13) << 4) |
                     (bit(o.noiseval, 11) << 3) | (bit(o.noiseval, 7) << 2) |
                     (bit(o.noiseval, 4) << 1) | bit(o.noiseval, 2);
      }

      // combined waveforms: AND, as TinySID (wrong for $50/$60, cheap)
      uint8_t wave = 0x80; // no waveform selected: silent, TinySID gives DC
      if (ctrl & WAVES) {
        wave = 0xff;
        if (ctrl & WAVE_TRI) {
          wave &= tri;
        }
        if (ctrl & WAVE_SAW) {
          wave &= saw;
        }
        if (ctrl & WAVE_PULSE) {
          wave &= pls;
        }
        if (ctrl & WAVE_NOISE) {
          wave &= o.noiseout;
        }
      }

      switch (o.envphase) {
        case ATTACK:
          o.env += attack[v];
          if (o.env >= ENV_MAX) {
            o.env = ENV_MAX;
            o.envphase = DECAY;
          }
          break;
        case DECAY:
          o.env -= decay[v];
          if (o.env <= sustain[v]) {
            o.env = sustain[v];
            o.envphase = SUSTAIN;
          }
          break;
        case SUSTAIN:
          // a lowered sustain level decays further, as on the real chip
          if (o.env != sustain[v]) {
            o.envphase = DECAY;
          }
          break;
        case RELEASE:
          o.env -= release[v];
          if (o.env < ENV_FLOOR) {
            o.env = ENV_FLOOR;
          }
          break;
      }

      if (v == 2 && !voice3) {
        continue;
      }
      const int32_t s = ((int32_t)wave - 0x80) * o.env >> 22;
      if (bit(res_filt, v)) {
        wet += s;
      } else {
        dry += s;
      }
    }

    // state variable filter: 3 multiplies per sample
    flt_h = wet * ONE - (flt_b >> 8) * rez - flt_l;
    flt_b += fmul(fc, flt_h);
    flt_l += fmul(fc, flt_b);

    wet = 0;
    if (mode_vol & LOWPASS) {
      wet += flt_l >> 16;
    }
    if (mode_vol & BANDPASS) {
      wet += flt_b >> 16;
    }
    if (mode_vol & HIGHPASS) {
      wet += flt_h >> 16;
    }

    int32_t s = (vol * (dry + wet) * OUT_MUL) >> OUT_SHIFT;
    if (s > SAMPLE_MAX) {
      s = SAMPLE_MAX;
    } else if (s < -SAMPLE_MAX) {
      s = -SAMPLE_MAX;
    }
    out[i] = (int16_t)s;
  }
}
