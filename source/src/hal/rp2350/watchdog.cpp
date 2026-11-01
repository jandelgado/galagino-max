#ifdef HAL_RP2350
#include "watchdog.h"
#include <hardware/watchdog.h>

namespace hal {

void watchdogWatch(TaskHandle, uint32_t seconds) {
  const uint32_t ms = seconds > 8 ? 8000 : seconds * 1000;
  watchdog_enable(ms, true);
}

void watchdogFeed() { watchdog_update(); }

void watchdogUnwatch(TaskHandle) { watchdog_disable(); }

} // namespace hal
#endif // HAL_RP2350
