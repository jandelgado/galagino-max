#ifndef HAL_RP2350_AUDIO_H
#define HAL_RP2350_AUDIO_H

#include <stdint.h>
#include "../audio_config.h"

namespace hal {

// HAL_AUDIO_DVI: HDMI audio in the DVI stream (audio_dvi.cpp).
// HAL_AUDIO_NULL: timed silent sink (../audio_null.cpp).
class AudioOut {
public:
  static void init(const AudioConfig &cfg);
  static bool write(const int16_t *samples);
  static void writeBlocking(const int16_t *samples);
  static void flushSilence();
};

} // namespace hal

#endif // HAL_RP2350_AUDIO_H
