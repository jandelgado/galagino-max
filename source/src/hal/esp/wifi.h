#ifndef HAL_ESP_WIFI_H
#define HAL_ESP_WIFI_H

#include <stdint.h>

namespace hal {

// See wifi.cpp. Built only with ASSETS_REMOTE (needs -Wl,--wrap=free).
namespace Wifi {
  // Driver heap and task stacks: driver peak ~33.5 KB + WiFi task stack +
  // TCB, plus RX buffering while flash writes slow the receiver (peak
  // 47.2 KB measured at 48 KB).
  static constexpr uint32_t HEAP_BYTES = 65536;

  // false: stop receiving
  typedef bool (*Receive)(void *ctx, const uint8_t *data, uint32_t length);

  // Starts connecting and returns. heap: HEAP_BYTES, 4 byte aligned, owned
  // by the driver until down() returns. False if the driver did not start;
  // still call down().
  bool begin(const char *ssid, const char *pass, uint8_t *heap, uint32_t heapBytes);
  // Waits until connected, at most timeoutMs after begin(). On false, still
  // call down().
  bool up(uint32_t timeoutMs);
  // http://a.b.c.d[:port]/path (no DNS). Hands what recv() delivers, in
  // chunks of up to bufBytes, to receive until it returns false or the
  // server closes. False if the request could not be sent.
  bool get(const char *url, uint8_t *buf, uint32_t bufBytes, Receive receive, void *ctx);
  // Aborts if driver memory is left in the heap.
  void down();
}

} // namespace hal

#endif // HAL_ESP_WIFI_H
