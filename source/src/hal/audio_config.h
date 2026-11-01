#ifndef HAL_AUDIO_CONFIG_H
#define HAL_AUDIO_CONFIG_H

#include <stdint.h>

namespace hal {

struct AudioConfig {
  uint32_t rate;     // samples per second, 24000
  uint32_t chunk;    // samples per write(), 64
  uint32_t buffers;  // chunks the sink holds before write() refuses
};

} // namespace hal

#endif // HAL_AUDIO_CONFIG_H
