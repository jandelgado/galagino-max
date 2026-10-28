#ifndef HAL_ATTR_H
#define HAL_ATTR_H

// Code and data placement. Only header with per-platform branches (macros
// cannot live in a .cpp).
//   HAL_FAST_CODE  hot code in RAM
//   HAL_FAST_DATA  const tables read in hot loops, kept out of flash
//   HAL_HOT_CODE   hot code in RAM only where both cores share one flash
//                  cache (RP2350: 16 KB XIP, a core's misses evict the
//                  other's code); flash on ESP32 (a cache per core, IRAM
//                  scarce)
//   HAL_RETAINED   variable that survives a reset (not initialized at boot)

#if defined(HAL_ESP)
#include <esp_attr.h>
#define HAL_FAST_CODE IRAM_ATTR
#define HAL_FAST_DATA DRAM_ATTR
#define HAL_HOT_CODE
#define HAL_RETAINED RTC_NOINIT_ATTR
#elif defined(HAL_RP2350)
#define HAL_FAST_CODE __attribute__((section(".time_critical.galagino")))
#define HAL_FAST_DATA __attribute__((section(".data.galagino")))
#define HAL_HOT_CODE HAL_FAST_CODE
#define HAL_RETAINED
#else
#define HAL_FAST_CODE
#define HAL_FAST_DATA
#define HAL_HOT_CODE
#define HAL_RETAINED
#endif

// Generated tables are declared PROGMEM (AVR heritage). On the 32 bit targets
// const data is read from flash directly, so it expands to nothing.
#ifndef PROGMEM
#define PROGMEM
#endif

#endif // HAL_ATTR_H
