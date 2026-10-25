#include "watchdog.h"
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include <soc/soc_caps.h>

namespace hal {

// CONFIG_ESP_TASK_WDT_* build flags are ignored when the framework's sdkconfig
// is precompiled: arm the watchdog at runtime. Re-init per watched task, the
// new task has a new handle.
void watchdogWatch(TaskHandle task, uint32_t seconds) {
#if ESP_IDF_VERSION_MAJOR < 6
  esp_task_wdt_deinit();
  esp_task_wdt_init(seconds, true);
#else
  // idle tasks of all cores stay subscribed, as after the 4.4 init
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = seconds * 1000;
  cfg.idle_core_mask = (1u << SOC_CPU_CORES_NUM) - 1;
  cfg.trigger_panic = true;
  esp_task_wdt_reconfigure(&cfg);
#endif
  esp_task_wdt_add(task);
}

void watchdogFeed() {
#if ESP_IDF_VERSION_MAJOR >= 6
  // the first frame can end before watchdogWatch() ran: IDF 6 logs an error for that
  if (esp_task_wdt_status(NULL) != ESP_OK) {
    return;
  }
#endif
  esp_task_wdt_reset();
}

void watchdogUnwatch(TaskHandle task) { esp_task_wdt_delete(task); }

void idleWatchdogEnable(int core, bool on) {
#if ESP_IDF_VERSION_MAJOR < 6
  TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(core);
#else
  TaskHandle_t idle = xTaskGetIdleTaskHandleForCore(core);
#endif
  if (on) {
    esp_task_wdt_add(idle);
  } else {
    esp_task_wdt_delete(idle);
  }
}

} // namespace hal
