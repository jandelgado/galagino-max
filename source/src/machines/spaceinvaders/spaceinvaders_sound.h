#ifndef SPACEINVADERS_SOUND_H
#define SPACEINVADERS_SOUND_H

#include <stdint.h>
#include "../../emulation/ima_adpcm.h"

// Space Invaders discrete audio (MAME mw8080bw_a.cpp). regs[0] port 3:
// UFO(0) Shot(1) Explosion(2) InvaderDie(3) ExtPlay(4); regs[1] port 5:
// Fleet1-4(0-3) UFOhit(4); regs[2] coin insert (cleared here). Shot and
// invader-die are 12 kHz IMA-ADPCM samples.
class SpaceInvadersSound {
public:
  SpaceInvadersSound(const uint8_t *shot, uint32_t shot_len, const uint8_t *invhit, uint32_t invhit_len);
  void render(uint8_t *regs, int16_t *out, int n);

private:
  const uint8_t *shot_data;
  uint32_t shot_len;
  const uint8_t *invhit_data;
  uint32_t invhit_len;
  // Noise LFSR: 17-bit, taps at bits 4+16, clocked at 7515 Hz
  uint32_t noise_rng = 0x1FFFF;  // 17-bit LFSR
  int noise_clock = 0;           // noise clock accumulator
  int noise_out = 0;             // current noise output (bit 12)
  // UFO: SN76477 - SLF triangle ~5.3Hz modulates VCO 1220-3700Hz
  uint32_t ufo_sweep = 0;        // SLF position (0..4527)
  uint32_t ufo_cnt = 0;          // VCO phase accumulator
  int ufo_toggle = 1;            // VCO square wave state
  // Shot: sample playback
  ima_adpcm::Voice shot;         // 12 kHz IMA-ADPCM sample
  int shot_playing = 0;          // 1 if playing
  // Explosion: noise burst with slow decay (~2s)
  uint32_t explo_cnt = 0;        // decay tick counter
  int explo_env = 0;             // envelope level (decays from 120)
  // Invader die: sample playback
  ima_adpcm::Voice invhit;       // 12 kHz IMA-ADPCM sample
  int invhit_playing = 0;        // 1 if playing
  // Fleet: 4 low tones (55/40/37/33 Hz, doubled for small speaker)
  uint32_t fleet_cnt = 0;        // tone phase accumulator
  int fleet_toggle = 1;          // tone square wave state
  // UFO hit: descending warble tone ~2000Hz
  uint32_t ufohit_cnt = 0;       // tone phase accumulator
  int ufohit_toggle = 1;         // tone square wave state
  int ufohit_freq = 0;           // current frequency (descends)
  uint32_t ufohit_warble = 0;    // warble phase counter
  // Coin insert: metallic clink sound
  uint32_t coin_cnt = 0;         // tone phase accumulator
  uint32_t coin_cnt2 = 0;        // overtone phase accumulator
  int coin_toggle = 1;           // primary tone square wave
  int coin_toggle2 = 1;          // overtone square wave
  int coin_timer = 0;            // samples remaining for coin sound
  int coin_env = 0;              // envelope level (decays)
};

#endif
