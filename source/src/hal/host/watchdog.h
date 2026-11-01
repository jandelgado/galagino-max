#ifndef HAL_HOST_WATCHDOG_H
#define HAL_HOST_WATCHDOG_H

#include <stdint.h>
#include "rtos.h"

namespace hal {

inline void watchdogWatch(TaskHandle, uint32_t) {}
inline void watchdogFeed() {}
inline void watchdogUnwatch(TaskHandle) {}
inline void idleWatchdogEnable(int, bool) {}

} // namespace hal

#endif // HAL_HOST_WATCHDOG_H
