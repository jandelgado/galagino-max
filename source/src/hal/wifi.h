#ifndef HAL_WIFI_H
#define HAL_WIFI_H

// hal::Wifi: station mode and one plain HTTP GET, for the game loader of
// remote builds. Not part of hal.h: only the loader includes it.

#if defined(HAL_ESP)
#include "esp/wifi.h"
#elif defined(HAL_RP2350)
#include "rp2350/wifi.h"
#elif defined(HAL_HOST)
#include "host/wifi.h"
#else
#error "no hal::Wifi backend for this platform"
#endif

#endif // HAL_WIFI_H
