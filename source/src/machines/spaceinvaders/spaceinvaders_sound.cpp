#include "spaceinvaders_sound.h"

SpaceInvadersSound::SpaceInvadersSound(const uint8_t *shot, uint32_t shot_len, const uint8_t *invhit, uint32_t invhit_len)
    : shot_data(shot), shot_len(shot_len), invhit_data(invhit), invhit_len(invhit_len) {}

void SpaceInvadersSound::render(uint8_t *regs, int16_t *out, int n) {
  // Space Invaders discrete audio (based on MAME mw8080bw_a.cpp)

  // 12 kHz samples at 24 kHz output
  static const uint8_t SI_SAMPLE_HOLD = 2;

  uint8_t p3 = regs[0]; // port 3: UFO(0) Shot(1) Explosion(2) InvaderDie(3) ExtPlay(4)
  uint8_t p5 = regs[1]; // port 5: Fleet1(0) Fleet2(1) Fleet3(2) Fleet4(3) UFOhit(4)

  // Fleet: pick highest active bit → tone frequency (Hz)
  // Original hardware: 555 timer ~33-55Hz, doubled for small speaker audibility
  //const int fleet_freq[4] = { 66, 110, 80, 74 };
  const int fleet_freq[4] = { 37, 55, 48, 41};
  int fleet_f = 0;
  for(int b = 3; b >= 0; b--) {
    if(p5 & (1 << b)) { fleet_f = fleet_freq[b]; break; }
  }

  for(int i = 0; i < n; i++) {
    int16_t value = 0;

    // ── Advance noise LFSR: 17-bit, taps 4+16, clock 7515 Hz ──
    noise_clock += 7515;
    while(noise_clock >= 24000) {
      noise_clock -= 24000;
      int bit = ((noise_rng >> 4) ^ (noise_rng >> 16)) & 1;
      noise_rng = ((noise_rng << 1) | bit) & 0x1FFFF;
      noise_out = (noise_rng >> 12) & 1;
    }

    // ── UFO: SN76477 – SLF triangle ~5.3Hz modulates VCO 1220-3700Hz ──
    if(p3 & 0x01) {
      // SLF triangle: full cycle = 24000/5.3 ≈ 4528 samples
      ufo_sweep = (ufo_sweep + 1) % 4528;
      int slf_pos = (ufo_sweep < 2264) ? ufo_sweep : (4528 - ufo_sweep);
      int vco_freq = 1220 + (int)((long)slf_pos * 2480 / 2264);
      // VCO square wave: counter += freq, toggle at 12000 (= 24kHz/2)
      ufo_cnt += vco_freq;
      if(ufo_cnt >= 12000) {
        ufo_cnt -= 12000;
        ufo_toggle = -ufo_toggle;
      }
      value += ufo_toggle * 60;
    }
    else {
      ufo_sweep = 0;
    }

    // ── SHOT: original sample playback (12kHz samples, play each twice for 24kHz) ──
    if(p3 & 0x02) {
      if(!shot_playing) {
        shot_playing = 1;
        shot.start(shot_data, shot_len, SI_SAMPLE_HOLD);
      }
      value += shot.next() * 3;
    }
    else {
      shot_playing = 0;
    }

    // ── COIN INSERT: metallic clink (triggered via soundregs[2]) ──
    if(regs[2] && coin_timer == 0) {
      coin_timer = 360;  // ~15ms
      coin_env = 120;
      regs[2] = 0;
    }
    if(coin_timer > 0) {
      // Primary metallic tone: 4500Hz
      coin_cnt += 4500;
      if(coin_cnt >= 12000) {
        coin_cnt -= 12000;
        coin_toggle = -coin_toggle;
      }
      // Overtone for metallic ring: 9500Hz
      coin_cnt2 += 9500;
      if(coin_cnt2 >= 12000) {
        coin_cnt2 -= 12000;
        coin_toggle2 = -coin_toggle2;
      }
      value += (coin_toggle * coin_env + coin_toggle2 * (coin_env / 2)) / 2;
      coin_timer--;
      if((coin_timer % 12) == 0 && coin_env > 5) coin_env--;
    }

    // ── EXPLOSION: noise burst with slow decay (RC ~2.7s) ──
    if(p3 & 0x04) {
      if(explo_env == 0) explo_env = 120;  // init on trigger
      int noise = noise_out ? 1 : -1;
      value += noise * explo_env;
      // Slow decay: decrease envelope every ~50 samples (~2ms)
      explo_cnt++;
      if(explo_cnt >= 50) {
        explo_cnt = 0;
        if(explo_env > 15) explo_env--;
      }
    }
    else {
      explo_env = 0;
      explo_cnt = 0;
    }

    // ── INVADER DIE: original sample playback (12kHz samples, play each twice for 24kHz) ──
    if(p3 & 0x08) {
      if(!invhit_playing) {
        invhit_playing = 1;
        invhit.start(invhit_data, invhit_len, SI_SAMPLE_HOLD);
      }
      value += invhit.next() * 3;
    }
    else {
      invhit_playing = 0;
    }

    // ── FLEET MOVEMENT: low bass tone while any fleet bit set ──
    if(fleet_f > 0) {
      fleet_cnt += fleet_f;
      if(fleet_cnt >= 12000) {
        fleet_cnt -= 12000;
        fleet_toggle = -fleet_toggle;
      }
      value += fleet_toggle * 100;
    }

    // ── UFO HIT: descending warble tone ~2000Hz with ~15Hz modulation ──
    if(p5 & 0x10) {
      if(ufohit_freq == 0) ufohit_freq = 2000;  // init on trigger
      // Warble modulation at ~15Hz: amplitude ±200Hz
      ufohit_warble = (ufohit_warble + 1) % 1600;  // 24000/15 = 1600
      int warble_pos = (ufohit_warble < 800) ?
        (int)ufohit_warble : (int)(1600 - ufohit_warble);
      int mod_freq = ufohit_freq + (warble_pos * 400 / 800 - 200);
      if(mod_freq < 100) mod_freq = 100;
      ufohit_cnt += mod_freq;
      if(ufohit_cnt >= 12000) {
        ufohit_cnt -= 12000;
        ufohit_toggle = -ufohit_toggle;
      }
      value += ufohit_toggle * 100;
      // Descend (~2000→300 over ~1.5s = 36000 samples)
      if(ufohit_freq > 300) ufohit_freq--;
    }
    else {
      ufohit_freq = 0;
      ufohit_warble = 0;
    }

    // Clamp
    if(value > 500) value = 500;
    if(value < -500) value = -500;

    out[i] = value;
  }
}
