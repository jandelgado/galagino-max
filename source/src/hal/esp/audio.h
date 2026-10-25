#ifndef HAL_ESP_AUDIO_H
#define HAL_ESP_AUDIO_H

#include <stdint.h>
#include <esp_idf_version.h>
#include "../audio_config.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 4)
// See https://github.com/espressif/arduino-esp32/issues/8467
#define WORKAROUND_I2S_APLL_PROBLEM
// as a workaround we run dkong audio also at 24khz (instead of 11765)
// and fill the buffer at half the rate resulting in 12khz effective
// sample rate
#endif

#ifndef WORKAROUND_I2S_APLL_PROBLEM
// Machines render 24 kHz only (dkong doubles its 11765 Hz DAC itself).
#error "galagino audio needs ESP-IDF >= 4.4.4: fixed 24 kHz output"
#endif

namespace hal {

// I2S sink: built-in DAC, or ES8311 codec (ES8311_AUDIO). Samples are signed
// 16 bit mono after volume; the backend converts to its output format.
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
