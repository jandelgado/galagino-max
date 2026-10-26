#ifndef HAL_HOST_WIFI_H
#define HAL_HOST_WIFI_H

#include <stdint.h>

namespace hal {

// No network on the host: the driver never starts.
namespace Wifi {
  static constexpr uint32_t HEAP_BYTES = 65536;
  typedef bool (*Receive)(void *ctx, const uint8_t *data, uint32_t length);

  inline bool begin(const char *, const char *, uint8_t *, uint32_t) { return false; }
  inline bool up(uint32_t) { return false; }
  inline bool get(const char *, uint8_t *, uint32_t, Receive, void *) { return false; }
  inline void down() { }
}

} // namespace hal

#endif // HAL_HOST_WIFI_H
