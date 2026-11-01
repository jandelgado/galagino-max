#ifndef HAL_RP2350_WIFI_H
#define HAL_RP2350_WIFI_H

#include <stdint.h>

namespace hal {

// See wifi.cpp. Pico 2 W remote builds only (CYW43, lwIP).
namespace Wifi {
  // lwIP memory is static (lwipopts.h): the caller's heap stays unused
  static constexpr uint32_t HEAP_BYTES = 4;

  // false: stop receiving
  typedef bool (*Receive)(void *ctx, const uint8_t *data, uint32_t length);

  // Starts connecting and returns. False if the driver did not start; still
  // call down().
  bool begin(const char *ssid, const char *pass, uint8_t *heap, uint32_t heapBytes);
  // Waits until connected with an IP, at most timeoutMs after begin().
  bool up(uint32_t timeoutMs);
  // http://a.b.c.d[:port]/path (no DNS). Hands what recv() delivers, in
  // chunks of up to bufBytes, to receive until it returns false or the
  // server closes. False if the request could not be sent.
  bool get(const char *url, uint8_t *buf, uint32_t bufBytes, Receive receive, void *ctx);
  // Leaves the network; the CYW43 stays on for Bluetooth.
  void down();
}

} // namespace hal

#endif // HAL_RP2350_WIFI_H
