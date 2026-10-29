#ifndef HAL_FLASH_REGION_H
#define HAL_FLASH_REGION_H

// hal::FlashRegion: an erasable, writable, memory-mappable flash area (the
// assets partition of remote builds). Not part of hal.h: only the asset
// cache and the loader include it.

#if defined(HAL_ESP)
#include "esp/flash_region.h"
#elif defined(HAL_RP2350)
#include "rp2350/flash_region.h"
#elif defined(HAL_HOST)
#include "host/flash_region.h"
#else
#error "no hal::FlashRegion backend for this platform"
#endif

#endif // HAL_FLASH_REGION_H
