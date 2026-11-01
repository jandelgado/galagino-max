#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

// hal::AudioOut selection, the audio axis of the build: one of
// HAL_AUDIO_ESP_DAC (built-in DAC), HAL_AUDIO_ES8311 (codec over I2S),
// HAL_AUDIO_NULL (silent sink with the timing of a real one; host: test sink).
// config.h chooses it (board profile or platform default). Backend
// sources include this header and compile only for their define.

#include "../config.h"

// AudioOut has one declaration per platform; the define picks the .cpp.
#if defined(HAL_ESP)
#include "esp/audio.h"
#elif defined(HAL_RP2350)
#include "rp2350/audio.h"
#elif defined(HAL_HOST)
#include "host/audio.h"
#endif

#endif // HAL_AUDIO_H
