#ifndef CODEC_ES8311_H
#define CODEC_ES8311_H

#include <stdint.h>

// ES8311 mono codec (Freenove FNK0104) over I2C: I2S slave, 16 bit input,
// MCLK = 256 x rate from the MCLK pin. Only the DAC path. The I2S stream is
// the audio backend's job.
class Es8311 {
public:
  // Reset, clocks, format, power up, volume 0..100. False on the first failed
  // transfer or for a rate without a divider entry.
  static bool init(int port, uint8_t addr, uint32_t rate, uint8_t volume);
};

#endif // CODEC_ES8311_H
