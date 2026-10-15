#include "galaxian_sound.h"

void GalaxianSound::render(const uint8_t *regs, int16_t *out, int n) {
  // Galaxian discrete sound hardware emulation (MAME galaxian_a.cpp)
  // SOUND_CLOCK = 18.432MHz/6/2 = 1.536MHz
  //
  // soundregs[0]    = VCO pitch (8-bit, written at 0x7800)
  // soundregs[1-4]  = LFO DAC bits (4-bit, 0x6004-0x6007)
  // soundregs[8-10] = FS1/FS2/FS3 background tone enables (0x6800-0x6802)
  // soundregs[11]   = HIT noise enable (0x6803)
  // soundregs[12]   = (unused, offset 4 not wired)
  // soundregs[13]   = FIRE shoot enable (0x6805, offset 5)
  // soundregs[14]   = VOL1 (0x6806, offset 6)
  // soundregs[15]   = VOL2 (0x6807, offset 7)
  // NOTE: No BGEN register — VCO is always active when pitch is audible

  // VOL1/VOL2 control VCO output volume via resistor network
  // Volumcontrol not needed - every sound has its own volume setting here
  uint8_t vol1On = regs[14];  // offset 6
  uint8_t vol2On = regs[15];  // offset 1

  // VCO half-period: freq = 1.536MHz / (16*(256-pitch))
  // At 24kHz: half_period = (256-pitch) / 8
  uint8_t vco_pitch = regs[0];
  uint8_t half_period = (256 - vco_pitch);

  // Detect pitch sweeps (credit sound): VCO plays through R34 base path
  // when pitch is actively changing even without VOL1/VOL2
  if(vco_pitch != last_pitch) {
    pitch_active = 500;  // sustain ~20ms (just over 1 frame)
    last_pitch = vco_pitch;
  }
  if(pitch_active > 0) pitch_active--;

  // VCO plays when: VOL is on (normal sounds) OR pitch is sweeping (credit sound)
  bool vco_on = (half_period > 1) && (vol1On || vol2On || pitch_active > 0);

  // FS1/FS2/FS3: 555 timer tones (frequencies from RC values)
  // FS1 ~139Hz, FS2 ~190Hz, FS3 ~267Hz // {86, 63, 44}
  // 24.000Hz / 130Hz = 184 / 2 = 92
  // Half-periods at 24kHz sample rate
  static const uint8_t fs_period[3] = {86, 63, 44};
  uint8_t lfo_val = ((regs[1] & 0x01) << 0);
  lfo_val |= ((regs[2] & 0x01) << 1);
  lfo_val |= ((regs[3] & 0x01) << 2);
  lfo_val |= ((regs[4] & 0x01) << 3);

  if (lfo_val > 2) {
    lfo_counter++;
    if ((lfo_counter % (lfo_val * 3)) == 0) {
      lfo++;
      if (lfo > 10)
        lfo = 0;
    }
  }
  else {
    lfo = 0;
  }

  for(int i = 0; i < n; i++) {
    int16_t value = 0;

    // === VCO tone ===
    if(vco_on) {
      for (int k = 0; k < 8; k++) {
        tone_cnt++;
        if(tone_cnt >= half_period) {
          tone_cnt = 0;
          tone_toggle = -tone_toggle;
        }
      }
      value += tone_toggle * 100;
    }

    // === FS1, FS2, FS3: background march tones (independent volume) ===
    for(int fs = 0; fs < 3; fs++) {
      if(regs[8 + fs]) {
        fs_cnt[fs]++;
        if(fs_cnt[fs] >= fs_period[fs] + lfo) {
          fs_cnt[fs] = 0;
          fs_toggle[fs] = -fs_toggle[fs];
        }
        value += fs_toggle[fs] * 25;
      }
    }

    // === HIT: explosion noise (LFSR, bandpass ~470Hz) ===
    if(regs[11]) {
      uint32_t b = ((noise_rng >> 0) ^ (noise_rng >> 3)) & 1;
      noise_rng = (noise_rng >> 1) | (b << 16);
      value += ((noise_rng & 1) ? 90 : -90);
    }

    // === FIRE: shooting sound (offset 5, 555 astable ~2.7kHz + noise) ===
    if(regs[13]) {
      fire_cnt++;
      if(fire_cnt >= 9) {
        fire_cnt = 0;
        uint32_t b = ((fire_rng >> 0) ^ (fire_rng >> 3)) & 1;
        fire_rng = (fire_rng >> 1) | (b << 16);
      }
      value += ((fire_rng & 1) ? 70 : -70);
    }

    out[i] = value;
  }
}
