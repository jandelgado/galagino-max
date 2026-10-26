#ifndef HAL_ESP_AUDIO_ENCODE_H
#define HAL_ESP_AUDIO_ENCODE_H

#include <stdint.h>

namespace hal {
namespace esp {

// Idle output level. The built-in DAC is unsigned: 0x0000 = 0 V, 0x8000 =
// VDD/2. Any other idle level steps the output on start/stop: click.
static const uint16_t SILENCE = 0x8000;

// Built-in DAC (dac_continuous): 8 bit, the driver widens byte k to the high
// byte of DMA word k. Mono plays the bytes in order: the old Arduino driver's
// pair swap made samples (Zaxxon, The Pit) sound distorted here.
// SND_DIFF keeps the old channel order (even bytes GPIO26 positive, odd
// GPIO25 inverted), not verified on a device.
inline void encodeDac8Mono(const int16_t *in, uint8_t *out, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    out[i] = (uint8_t)((uint16_t)(SILENCE + in[i]) >> 8);
  }
}

inline void encodeDac8Diff(const int16_t *in, uint8_t *out, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    out[2 * i] = (uint8_t)((uint16_t)(SILENCE + in[i]) >> 8);
    out[2 * i + 1] = (uint8_t)((uint16_t)(SILENCE - in[i]) >> 8);
  }
}

} // namespace esp
} // namespace hal

#endif // HAL_ESP_AUDIO_ENCODE_H
