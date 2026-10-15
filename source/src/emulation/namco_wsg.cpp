#include "namco_wsg.h"

void NamcoWsg::render(const uint8_t *regs, const NamcoWave *waves, int16_t *out, int n) {
  const signed char *wave[VOICES];
  uint32_t freq[VOICES];
  uint8_t volume[VOICES];
  for (int ch = 0; ch < VOICES; ch++) {
    wave[ch] = waves[regs[ch * 5 + 0x05] & 0x07];
    freq[ch] = (ch == 0) ? regs[0x10] : 0;  // 5050-5054, 5056-5059, 505b-505e
    freq[ch] += regs[ch * 5 + 0x11] << 4;
    freq[ch] += regs[ch * 5 + 0x12] << 8;
    freq[ch] += regs[ch * 5 + 0x13] << 12;
    freq[ch] += regs[ch * 5 + 0x14] << 16;
    volume[ch] = regs[ch * 5 + 0x15];  // 5055, 505a, 505f
  }

  for (int i = 0; i < n; i++) {
    int16_t value = 0;
    for (int ch = 0; ch < VOICES; ch++) {
      if (volume[ch]) {
        value += volume[ch] * wave[ch][(cnt[ch] >> 13) & 0x1f];
      }
      cnt[ch] += freq[ch];
    }
    out[i] = value;
  }
}

void Namco15xx::render(const uint8_t *regs, const NamcoWave *waves, bool enabled, int16_t *out, int n) {
  const signed char *wave[VOICES];
  uint32_t freq[VOICES];
  uint8_t volume[VOICES];
  for (int ch = 0; ch < VOICES; ch++) {
    wave[ch] = waves[(regs[ch * 8 + 6] >> 4) & 0x07];
    freq[ch] = regs[ch * 8 + 4] + (regs[ch * 8 + 5] << 8) + ((uint32_t)(regs[ch * 8 + 6] & 0x0f) << 16);
    volume[ch] = enabled ? (regs[ch * 8 + 3] & 0x0f) : 0;
  }

  for (int i = 0; i < n; i++) {
    int16_t value = 0;
    for (int ch = 0; ch < VOICES; ch++) {
      if (volume[ch]) {
        value += volume[ch] * wave[ch][(cnt[ch] >> 15) & 0x1f];
      }
      cnt[ch] += freq[ch];
    }
    out[i] = value;
  }
}
