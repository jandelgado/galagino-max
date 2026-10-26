#ifndef HAL_ESP_AUDIO_H
#define HAL_ESP_AUDIO_H

#include <stdint.h>
#include "../audio_config.h"

namespace hal {

// Built-in DAC (HAL_AUDIO_ESP_DAC, dac_continuous) or I2S to the ES8311 codec
// (HAL_AUDIO_ES8311). Selection: ../audio.h. Samples are signed 16 bit mono
// after volume; the backend converts to its output format.
class AudioOut {
public:
  // Ramps the output to its idle level: no click.
  static void init(const AudioConfig &cfg);
  // One chunk if a DMA buffer is free, else false (all or nothing).
  static bool write(const int16_t *samples);
  // Waits for a free DMA buffer.
  static void writeBlocking(const int16_t *samples);
  // Fills every DMA buffer with idle level: left alone, the DMA replays old
  // content in a loop and hums.
  static void flushSilence();
};

} // namespace hal

#endif // HAL_ESP_AUDIO_H
