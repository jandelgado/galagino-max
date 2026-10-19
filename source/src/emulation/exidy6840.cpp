#include "exidy6840.h"

#include <string.h>

void Exidy6840::reset(uint32_t sample_hz) {
  memset(m_timer, 0, sizeof(m_timer));
  memset(m_volume, 0, sizeof(m_volume));
  m_msb_latch = m_lsb_latch = 0;
  m_lfsr_oldxor = 0;
  for (int i = 0; i < 4; i++) {
    m_lfsr[i] = 0xffffffff;
  }
  m_clocks_per_step = (uint32_t)(((uint64_t)CLOCK_HZ << 24) / (sample_hz * SUBSTEPS));
  m_clock_count = 0;
  m_sfxctrl = 0;
}

// count down by clocks; output state toggles (16 bit) or goes high while
// the MSB is 0 (dual 8 bit)
void Exidy6840::apply_clock(Timer &t, int clocks) {
  if (t.cr & CR_DUAL_8BIT) {
    uint8_t lo = t.counter & 0xff, hi = t.counter >> 8;
    while (clocks > lo) {
      clocks -= lo + 1;
      lo = t.timer & 0xff;

      if (!hi--) {
        t.state = 0;
        lo = t.timer & 0xff;
        hi = t.timer >> 8;
      } else if (!hi) {
        t.state = 1;
        t.clocks++;
      }
    }
    lo -= clocks;
    t.counter = (uint16_t)(hi << 8 | lo);
    return;
  }

  while (clocks > t.counter) {
    clocks -= t.counter + 1;
    t.state ^= 1;
    t.clocks += t.state;
    t.counter = t.timer;
  }
  t.counter -= clocks;
}

// shift the 128 bit LFSR; returns the 0 -> 1 transitions at the tap (bit 96)
int Exidy6840::update_noise(int clocks) {
  int noise_clocks = 0;
  for (int i = 0; i < clocks; i++) {
    const uint32_t newxor = (m_lfsr[3] ^ m_lfsr[2]) >> 31;
    m_lfsr[3] = m_lfsr[3] << 1 | m_lfsr[2] >> 31;
    m_lfsr[2] = m_lfsr[2] << 1 | m_lfsr[1] >> 31;
    m_lfsr[1] = m_lfsr[1] << 1 | m_lfsr[0] >> 31;
    m_lfsr[0] = m_lfsr[0] << 1 | (newxor ^ m_lfsr_oldxor);
    m_lfsr_oldxor = newxor;
    if ((m_lfsr[2] & 0x03) == 0x01) {
      noise_clocks++;
    }
  }
  return noise_clocks;
}

// one MAME sound_stream_update sample with clocks 6840 clocks
int32_t Exidy6840::step(int clocks) {
  if (m_timer[0].cr & CR_ALL_RESET) {
    return 0;
  }

  // skip the LFSR unless a channel uses it
  const bool noisy = (m_timer[0].cr & m_timer[1].cr & m_timer[2].cr & CR_INTERNAL) == 0;
  int32_t sample = 0;
  int noise_clocks = 0;

  if (noisy && !(m_sfxctrl & SFX_NOISE_FROM_CH0)) {
    noise_clocks = update_noise(clocks);
  }

  Timer &t0 = m_timer[0];
  const uint32_t chan0_clocks = t0.clocks;
  apply_clock(t0, (t0.cr & CR_INTERNAL) ? clocks : noise_clocks);
  if (t0.state && !(m_sfxctrl & SFX_MUTE_CH0) && (t0.cr & CR_OUTPUT)) {
    sample += m_volume[0];
  }

  if (noisy && (m_sfxctrl & SFX_NOISE_FROM_CH0)) {
    noise_clocks = update_noise(t0.clocks - chan0_clocks);
  }

  Timer &t1 = m_timer[1];
  apply_clock(t1, (t1.cr & CR_INTERNAL) ? clocks : noise_clocks);
  if (t1.state && (t1.cr & CR_OUTPUT)) {
    sample += m_volume[1];
  }

  // channel 2 optionally divides by 8
  Timer &t2 = m_timer[2];
  int c2 = (t2.cr & CR_INTERNAL) ? clocks : noise_clocks;
  if (t2.cr & CR_ALL_RESET) {
    c2 += t2.leftovers;
    t2.leftovers = c2 % 8;
    c2 /= 8;
  }
  apply_clock(t2, c2);
  if (t2.state && (t2.cr & CR_OUTPUT)) {
    sample += m_volume[2];
  }
  return sample;
}

int32_t Exidy6840::sample() {
  int32_t sum = 0;
  for (int i = 0; i < SUBSTEPS; i++) {
    m_clock_count += m_clocks_per_step;
    const int clocks = m_clock_count >> 24;
    m_clock_count &= (1 << 24) - 1;
    sum += step(clocks);
  }
  return sum / SUBSTEPS;
}

uint8_t Exidy6840::ptm_r(uint8_t offset) {
  switch (offset) {
  case 0: // unused
  case 1: // status register, not emulated (MAME TODO)
    return 0;

  // channel 0..2 MSB, latches the LSB
  case 2:
  case 4:
  case 6: {
    const Timer &t = m_timer[(offset >> 1) - 1];
    m_lsb_latch = t.counter & 0xff;
    return t.counter >> 8;
  }

  default: // 3, 5, 7
    return m_lsb_latch;
  }
}

void Exidy6840::ptm_w(uint8_t offset, uint8_t data) {
  switch (offset) {
  case 0: // channel 0 or 2 control, selected by channel 1 CR bit 0
    m_timer[(m_timer[1].cr & CR_ALL_RESET) ? 0 : 2].cr = data;
    break;

  case 1:
    m_timer[1].cr = data;
    break;

  case 2:
  case 4:
  case 6:
    m_msb_latch = data;
    break;

  default: { // 3, 5, 7: LSB, latches the timer value
    Timer &t = m_timer[(offset - 3) / 2];
    t.timer = (uint16_t)(m_msb_latch << 8 | data);
    if (!(t.cr & CR_NO_PRELOAD)) {
      t.counter = t.timer;
    }
    break;
  }
  }
}

void Exidy6840::sfxctrl_w(uint8_t offset, uint8_t data) {
  if (offset == 0) {
    m_sfxctrl = data;
    return;
  }
  m_volume[offset - 1] = ((data & 7) * BASE_VOLUME) / 7;
}
