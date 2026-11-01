#ifndef HAL_RP2350_WATCHDOG_H
#define HAL_RP2350_WATCHDOG_H

#include <stdint.h>
#include "rtos.h"

namespace hal {

// Hardware watchdog: resets the chip after `seconds` (at most 8) without a
// feed. One watched task, as on the other backends.
void watchdogWatch(TaskHandle task, uint32_t seconds);
void watchdogFeed();
void watchdogUnwatch(TaskHandle task);
// FreeRTOS idle tasks are not watched here.
inline void idleWatchdogEnable(int, bool) {}

} // namespace hal

#endif // HAL_RP2350_WATCHDOG_H
