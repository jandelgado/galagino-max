#ifndef HAL_ESP_WATCHDOG_H
#define HAL_ESP_WATCHDOG_H

#include <stdint.h>
#include "rtos.h"

namespace hal {

// Task watchdog on one task; panics after `seconds` without a feed.
// Re-initializes the watchdog, so call once per watched task, not per feed.
void watchdogWatch(TaskHandle task, uint32_t seconds);
void watchdogFeed();                   // from the watched task
void watchdogUnwatch(TaskHandle task);

// Idle task of `core` in or out of the watchdog (a busy task pinned to that
// core would otherwise trip it).
void idleWatchdogEnable(int core, bool on);

} // namespace hal

#endif // HAL_ESP_WATCHDOG_H
