#ifndef WIFI_H
#define WIFI_H

class HttpBody;

// WiFi for the game loader, see wifi.cpp. Only while Arena is empty: up()
// takes WIFI_HEAP_BYTES of it, down() resets it.
namespace Wifi {
  // connected within 10 s; on false, still call down()
  bool up();
  // http://a.b.c.d[:port]/path; true if the whole body was delivered
  bool get(const char *url, HttpBody &body);
  void down();
}

#endif
