#include "zaxxon_sample_missile_homing.h"
#include "zaxxon_sample_missile_base.h"
#include "zaxxon_sample_laser.h"
#include "zaxxon_sample_battleship.h"
#include "zaxxon_sample_explosion_enemy.h"
#include "zaxxon_sample_explosion_ship.h"
#include "zaxxon_sample_cannon.h"
#include "zaxxon_sample_shot.h"
#include "zaxxon_sample_alarm_lock.h"
#include "zaxxon_sample_alarm_fuel.h"
#include "zaxxon_sample_noise_intro.h"
#include "zaxxon_sample_noise_asteroid.h"
#include "zaxxon_sound.h"
#include "../../emulation/ima_adpcm.h"

using ima_adpcm::ima_adpcm_decode;

static const int32_t OUT_MAX = 500;

// PPI-triggered discrete sample board (zaxxon_a.cpp zaxxon_sample_names):
// 12 channels, indexed exactly as MAME's sample table so ZaxxonSound can trigger
// them by index without knowing individual symbol names. Each points at a
// 4-bit IMA-ADPCM stream (2-byte predictor header + packed nibbles, see
// romconv/pyconv/adpcm.py), decoded per-sample by ZaxxonSound::render.
// Resolved through Asset in the ctor: a remote blob (ASSETS_REMOTE) has no
// data pointer of its own.
static const FlashAsset<unsigned char, PLAIN> *const zaxxon_sample_blob[12] = {
  &zaxxon_sample_missile_homing_blob,  // 0
  &zaxxon_sample_missile_base_blob,    // 1
  &zaxxon_sample_laser_blob,           // 2
  &zaxxon_sample_battleship_blob,      // 3
  &zaxxon_sample_explosion_enemy_blob, // 4 (S-Exp)
  &zaxxon_sample_explosion_ship_blob,  // 5 (M-Exp)
  &zaxxon_sample_cannon_blob,          // 6
  &zaxxon_sample_shot_blob,            // 7
  &zaxxon_sample_alarm_lock_blob,      // 8 (Alarm2)
  &zaxxon_sample_alarm_fuel_blob,      // 9 (Alarm3)
  &zaxxon_sample_noise_intro_blob,     // 10
  &zaxxon_sample_noise_asteroid_blob,  // 11
};
static const uint32_t zaxxon_sample_len[12] = {
  zaxxon_sample_missile_homing_blob.count,
  zaxxon_sample_missile_base_blob.count,
  zaxxon_sample_laser_blob.count,
  zaxxon_sample_battleship_blob.count,
  zaxxon_sample_explosion_enemy_blob.count,
  zaxxon_sample_explosion_ship_blob.count,
  zaxxon_sample_cannon_blob.count,
  zaxxon_sample_shot_blob.count,
  zaxxon_sample_alarm_lock_blob.count,
  zaxxon_sample_alarm_fuel_blob.count,
  zaxxon_sample_noise_intro_blob.count,
  zaxxon_sample_noise_asteroid_blob.count,
};
// output samples per stored sample, per channel (stored at 8-24 kHz)
static const uint8_t zaxxon_sample_hold[12] = {
  zaxxon_sample_missile_homing_hold,
  zaxxon_sample_missile_base_hold,
  zaxxon_sample_laser_hold,
  zaxxon_sample_battleship_hold,
  zaxxon_sample_explosion_enemy_hold,
  zaxxon_sample_explosion_ship_hold,
  zaxxon_sample_cannon_hold,
  zaxxon_sample_shot_hold,
  zaxxon_sample_alarm_lock_hold,
  zaxxon_sample_alarm_fuel_hold,
  zaxxon_sample_noise_intro_hold,
  zaxxon_sample_noise_asteroid_hold,
};

// Zaxxon: no tone/WSG chip at all, sound is 12 discrete circuits each
// triggered by one i8255 PPI output bit (zaxxon_a.cpp zaxxon_sound_a/b/c_w,
// ports A/B/C latched into soundregs[0..2] by zaxxon::wrZ80). All outputs
// are active-low: a falling edge (bit 1->0) starts a sample, a rising edge
// stops it for the five loop-capable channels (0/2/3/10/11); the rest play
// to completion once triggered. Channel numbering matches MAME's
// zaxxon_sample_names / zaxxon_sample_blob[] exactly.
ZaxxonSound::ZaxxonSound() {
  for (int ch = 0; ch < CHANNELS; ch++) {
    sample[ch] = Asset<unsigned char, PLAIN>(*zaxxon_sample_blob[ch]).data();
  }
}

void ZaxxonSound::startChannel(int ch, PlayMode mode) {
  active[ch] = true;
  loop[ch] = (mode == LOOP);
  pos[ch] = 0;
  phase[ch] = 0;
}

void ZaxxonSound::stopChannel(int ch) {
  active[ch] = false;
}

void ZaxxonSound::render(const uint8_t *regs, int16_t *out, int n) {
  const uint8_t a = regs[0];
  const uint8_t diff_a = a ^ prev;
  prev = a;

  // Port A: player-ship engine noise volume (bits 0-1) + 6 gated sounds.
  if ((diff_a & 0x04) && !(a & 0x04)) {
    startChannel(10, LOOP);
  }
  if ((diff_a & 0x04) && (a & 0x04)) {
    stopChannel(10);
  }
  if ((diff_a & 0x08) && !(a & 0x08)) {
    startChannel(11, LOOP);
  }
  if ((diff_a & 0x08) && (a & 0x08)) {
    stopChannel(11);
  }
  if ((diff_a & 0x10) && !(a & 0x10)) {
    startChannel(0, LOOP);
  }
  if ((diff_a & 0x10) && (a & 0x10)) {
    stopChannel(0);
  }
  if ((diff_a & 0x20) && !(a & 0x20)) {
    startChannel(1, ONE_SHOT);
  }
  if ((diff_a & 0x40) && !(a & 0x40)) {
    startChannel(2, LOOP);
  }
  if ((diff_a & 0x40) && (a & 0x40)) {
    stopChannel(2);
  }
  if ((diff_a & 0x80) && !(a & 0x80)) {
    startChannel(3, LOOP);
  }
  if ((diff_a & 0x80) && (a & 0x80)) {
    stopChannel(3);
  }

  // Ports B/C: explosions, cannon, shot, alarms. Pulsed by the ROM, so
  // triggered from the edge counters zaxxon::wrZ80 keeps (ZAXXON_TRIG_BASE).
  for (int ch = 4; ch <= 9; ch++) {
    const uint8_t n = regs[ZAXXON_TRIG_BASE + ch];
    if (n == trig_seen[ch - 4]) {
      continue;
    }
    trig_seen[ch - 4] = n;
    // M-Exp and Alarm3 don't restart while playing (MAME playing() check).
    if ((ch == 5 || ch == 9) && active[ch]) {
      continue;
    }
    startChannel(ch, ONE_SHOT);
  }

  // Engine noise (ch 10/11) volume: 2-bit level -> 0.5..0.971 gain, same
  // formula as MAME's set_volume(10/11, 0.5 + 0.157*(data&3)).
  const int engine_gain_q8 = 128 + 40 * (a & 0x03); // 0.5*256 .. 0.971*256

  for (int i = 0; i < n; i++) {
    int32_t value = 0;

    for (int ch = 0; ch < CHANNELS; ch++) {
      if (!active[ch]) {
        continue;
      }

      // decode the next stored sample at the start of its hold
      if (phase[ch] == 0) {
        adpcm_prev[ch] = adpcm_predictor[ch];
        if (pos[ch] == 0) {
          const uint8_t *enc = sample[ch];
          adpcm_predictor[ch] = (int16_t)(enc[0] | (enc[1] << 8));
          adpcm_step[ch] = 0;
          adpcm_prev[ch] = adpcm_predictor[ch];
        } else {
          uint32_t nibble = pos[ch] - 1;
          uint8_t packed = sample[ch][2 + (nibble >> 1)];
          uint8_t code = (nibble & 1) ? (packed >> 4) : (packed & 15);
          ima_adpcm_decode(adpcm_predictor[ch], adpcm_step[ch], code);
        }
      }

      // linear from the previous sample, reaching the current one at the
      // end of the hold (one stored sample late)
      const int32_t prev = adpcm_prev[ch];
      const int32_t step = phase[ch] + 1;
      const uint8_t hold = zaxxon_sample_hold[ch];
      int32_t s = (prev + (adpcm_predictor[ch] - prev) * step / hold) >> 8;
      if (ch == 10 || ch == 11) {
        s = (s * engine_gain_q8) >> 8;
      } else {
        s = s * 2;
      }
      value += s;

      if (++phase[ch] < hold) {
        continue;
      }
      phase[ch] = 0;

      if (++pos[ch] >= zaxxon_sample_len[ch]) {
        if (loop[ch]) {
          pos[ch] = 0;
        } else {
          active[ch] = false;
        }
      }
    }

    if (value > OUT_MAX) {
      value = OUT_MAX;
    } else if (value < -OUT_MAX) {
      value = -OUT_MAX;
    }
    out[i] = value;
  }
}
