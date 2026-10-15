#include "Arduino.h"  // PI
#include <math.h>
#include "bagman_sound.h"

// note frequencies (Hz) by note code
static const float A5_3 = 880.00;
static const float C6_4 = 1046.50;
static const float F5_5 = 739.989;
static const float G5_6 = 783.991;
static const float E6_7 = 1318.51;
static const float B6_8 = 1975.53; //??
static const float XX_B = 2100.00; //??
static const float D6_E = 1174.56;
static const float B5_F = 987.767;

static const int32_t AMPLITUDE = 256;

BagmanSound::BagmanSound() {
  for (int i = 0; i < WAVE_LEN; ++i) {
    sinus[i] = int32_t(float(AMPLITUDE) * sin(2.0 * PI * (1.0 / WAVE_LEN) * i));
  }
}

void BagmanSound::render(uint8_t *regs, int16_t *out, int n) {
  unsigned short duration = regs[0] + (regs[1] << 8);
  if (duration > 0)
    duration--;

  regs[0] = duration & 0x00ff;
  regs[1] = (duration & 0xff00) > 8;  // sic: > 8, not >> 8; kept, the ROM never sets a duration above 255

  float frequency = 0.0f;  // unknown note code: silence
  switch (regs[2]) {
    case 0x3: frequency = A5_3; break;
    case 0x4: frequency = C6_4; break;
    case 0x5: frequency = F5_5; break;
    case 0x6: frequency = G5_6; break;
    case 0x7: frequency = E6_7; break;
    case 0x8: frequency = B6_8; break;
    case 0xE: frequency = D6_E; break;
    case 0xF: frequency = B5_F; break;
    case 0xB: frequency = XX_B; break;
  }

  unsigned short pause = regs[3];
  if (pause > 0)
    regs[3]--;

  float delta = 0;
  if (duration != 0 && pause == 0)
    delta = (frequency * (WAVE_LEN)) / float(24000);

  for(int i = 0; i < n; i++) {
    uint16_t pos = uint32_t(((i + 1) * delta) + position_last) % (WAVE_LEN);
    int16_t value = sinus[pos];

    if (i == n - 1)
      position_last = pos;

    out[i] = value;
  }
}
