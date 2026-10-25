#ifndef HAL_ESP_AUDIO_ENCODE_H
#define HAL_ESP_AUDIO_ENCODE_H

#include <stdint.h>

namespace hal {
namespace esp {

// Idle output level. The built-in DAC is unsigned: 0x0000 = 0 V, 0x8000 =
// VDD/2. Any other idle level steps the output on start/stop: click.
static const uint16_t SILENCE = 0x8000;

// Mono: bias the signed sample to SILENCE. The index ^ 1 swaps neighbours, a
// work-around for the byte order bug of the I2S driver, see
// https://github.com/espressif/arduino-esp32/issues/8467#issuecomment-1656616015
inline void encodeMono(const int16_t *in, uint16_t *out, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    out[i ^ 1] = (uint16_t)(SILENCE + in[i]);
  }
}

// SND_DIFF: positive signal on GPIO26, inverted on GPIO25; 2 n words out.
inline void encodeDiff(const int16_t *in, uint16_t *out, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    out[2 * i] = (uint16_t)(SILENCE + in[i]);
    out[2 * i + 1] = (uint16_t)(SILENCE - in[i]);
  }
}

} // namespace esp
} // namespace hal

#endif // HAL_ESP_AUDIO_ENCODE_H
