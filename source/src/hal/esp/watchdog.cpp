#include "watchdog.h"
#include <esp_task_wdt.h>

namespace hal {

// CONFIG_ESP_TASK_WDT_* build flags are ignored when the framework's sdkconfig
// is precompiled: arm the watchdog at runtime. Re-init per watched task, the
// new task has a new handle.
void watchdogWatch(TaskHandle task, uint32_t seconds) {
  esp_task_wdt_deinit();
  esp_task_wdt_init(seconds, true);
  esp_task_wdt_add(task);
}

void watchdogFeed() { esp_task_wdt_reset(); }

void watchdogUnwatch(TaskHandle task) { esp_task_wdt_delete(task); }

void idleWatchdogEnable(int core, bool on) {
  TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(core);
  if (on) {
    esp_task_wdt_add(idle);
  } else {
    esp_task_wdt_delete(idle);
  }
}

} // namespace hal
