// WiFi for the game loader (ASSETS_REMOTE builds) over the Pico 2 W's CYW43:
// station up, one HTTP GET over an lwIP socket, station down. Only between
// game select and game ctor.
//
//   main task (video core)      async_context task (video core, cyw43.h)
//   begin(): join, returns      CYW43 driver, BTstack, lwIP input, DHCP
//   up(): poll link status      tcpip thread (video core): lwIP timers,
//   get(): socket calls ------> socket calls under the core lock
//   down(): leave
//
// The CYW43 and lwIP start once (cyw43Begin), shared with the Bluetooth
// pad; lwIP has no deinit, so its memory is static (lwipopts.h).
#ifdef HAL_RP2350
#include "../../config.h"
#ifdef ASSETS_REMOTE
#include "wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <FreeRTOS.h>
#include <pico/cyw43_arch.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include "cyw43.h"
#include "rtos.h"
#include "time.h"

namespace hal {
namespace {

const uint16_t HTTP_DEFAULT_PORT = 80;
const uint32_t RECV_TIMEOUT_S = 5;
const uint32_t CONNECT_TCP_TIMEOUT_S = 5;
const uint32_t LINK_POLL_MS = 20;
const uint32_t REJOIN_PAUSE_MS = 1000;

uint32_t connectStart;
bool started;
const char *joinSsid;
const char *joinPass;

bool join() {
  if (cyw43_arch_wifi_connect_async(joinSsid, joinPass, CYW43_AUTH_WPA2_AES_PSK)) {
    printf("wifi: join failed\n");
    return false;
  }
  return true;
}

// Blocking connect() retries SYN/ARP for a long time when the host is
// unreachable: connect non-blocking, wait at most CONNECT_TCP_TIMEOUT_S.
bool connectTimeout(int s, const sockaddr_in &addr) {
  const int flags = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);
  if (connect(s, (const sockaddr *)&addr, sizeof(addr)) && errno != EINPROGRESS) {
    return false;
  }

  fd_set writable;
  FD_ZERO(&writable);
  FD_SET(s, &writable);
  timeval timeout = {CONNECT_TCP_TIMEOUT_S, 0};
  if (select(s + 1, nullptr, &writable, nullptr, &timeout) != 1) {
    return false;
  }

  int err = 0;
  socklen_t len = sizeof(err);
  getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
  fcntl(s, F_SETFL, flags);
  return err == 0;
}

} // namespace

bool Wifi::begin(const char *ssid, const char *pass, uint8_t *, uint32_t) {
  started = false;
  if (!cyw43Begin()) {
    printf("wifi: no CYW43\n");
    return false;
  }
  btScan(BtScan::PAUSED);

  cyw43_arch_enable_sta_mode();
  // no power save while downloading: it throttles the receive rate
  cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
  joinSsid = ssid;
  joinPass = pass;
  connectStart = millis();
  started = join();
  return started;
}

bool Wifi::up(uint32_t timeoutMs) {
  if (!started) {
    return false;
  }
  int status = CYW43_LINK_DOWN;
  uint32_t joins = 1;
  while (millis() - connectStart < timeoutMs) {
    status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (status == CYW43_LINK_UP) {
      break;
    }

    // a failed join (no AP seen, AP deauth right after association): again
    if (status < 0) {
      sleepMs(REJOIN_PAUSE_MS);
      joins += join();
    }
    sleepMs(LINK_POLL_MS);
  }
  if (status != CYW43_LINK_UP) {
    printf("wifi: not connected, link status %d, %u joins\n", status, (unsigned)joins);
    return false;
  }
  printf("wifi: connected %u ms after begin, %u joins, ip %s\n", (unsigned)(millis() - connectStart),
         (unsigned)joins, ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));
  return true;
}

// url: http://a.b.c.d[:port]/path (no DNS)
bool Wifi::get(const char *url, uint8_t *buf, uint32_t bufBytes, Receive receive, void *ctx) {
  static const char PREFIX[] = "http://";
  if (strncmp(url, PREFIX, sizeof(PREFIX) - 1)) {
    printf("wifi: bad url %s\n", url);
    return false;
  }
  char host[64];
  const char *h = url + sizeof(PREFIX) - 1;
  const char *slash = strchr(h, '/');
  const char *path = slash ? slash : "/";
  const size_t hostLen = slash ? slash - h : strlen(h);
  snprintf(host, sizeof(host), "%.*s", (int)hostLen, h);
  uint16_t port = HTTP_DEFAULT_PORT;
  char *colon = strchr(host, ':');
  if (colon) {
    *colon = 0;
    port = atoi(colon + 1);
  }

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (!inet_aton(host, &addr.sin_addr)) {
    printf("wifi: host must be an IP: %s\n", host);
    return false;
  }

  const int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0 || !connectTimeout(s, addr)) {
    printf("wifi: tcp connect failed, errno %d\n", errno);
    if (s >= 0) {
      close(s);
    }
    return false;
  }

  // a stalled server ends the download instead of hanging the loader
  const timeval timeout = {RECV_TIMEOUT_S, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  char req[160];
  const int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
  send(s, req, n, 0);

  int r;
  while ((r = recv(s, buf, bufBytes, 0)) > 0 && receive(ctx, buf, r)) {
  }
  close(s);
  return true;
}

void Wifi::down() {
  if (cyw43Begin()) {
    cyw43_arch_disable_sta_mode();
    btScan(BtScan::RESUMED);
  }
  started = false;
  printf("wifi: down, FreeRTOS heap free %u, min %u\n", (unsigned)xPortGetFreeHeapSize(),
         (unsigned)xPortGetMinimumEverFreeHeapSize());
}

} // namespace hal
#endif // ASSETS_REMOTE
#endif // HAL_RP2350
