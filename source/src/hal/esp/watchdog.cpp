#include "watchdog.h"
#include <esp_task_wdt.h>
#include <soc/soc_caps.h>

namespace hal {

// CONFIG_ESP_TASK_WDT_* build flags are ignored when the framework's sdkconfig
// is precompiled: arm the watchdog at runtime. Re-init per watched task, the
// new task has a new handle.
void watchdogWatch(TaskHandle task, uint32_t seconds) {
  // idle tasks of all cores stay subscribed
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = seconds * 1000;
  cfg.idle_core_mask = (1u << SOC_CPU_CORES_NUM) - 1;
  cfg.trigger_panic = true;
  esp_task_wdt_reconfigure(&cfg);
  esp_task_wdt_add(task);
}

void watchdogFeed() {
  // the first frame can end before watchdogWatch() ran: IDF 6 logs an error for that
  if (esp_task_wdt_status(NULL) != ESP_OK) {
    return;
  }
  esp_task_wdt_reset();
}

void watchdogUnwatch(TaskHandle task) { esp_task_wdt_delete(task); }

void idleWatchdogEnable(int core, bool on) {
  TaskHandle_t idle = xTaskGetIdleTaskHandleForCore(core);
  if (on) {
    esp_task_wdt_add(idle);
  } else {
    esp_task_wdt_delete(idle);
  }
}

} // namespace hal
