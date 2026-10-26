#ifndef HAL_ESP_RTOS_H
#define HAL_ESP_RTOS_H

#include <stdint.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace hal {

// The video core runs the main task (display, audio refill); the emulation
// task runs on the other one.
static const int VIDEO_CORE = CONFIG_ESP_MAIN_TASK_AFFINITY;
static const int EMULATION_CORE = 1 - VIDEO_CORE;

static const uint32_t WAIT_FOREVER = 0xFFFFFFFFu;

typedef TaskHandle_t TaskHandle;

// Returns NULL if the task could not be created.
TaskHandle taskSpawn(void (*fn)(void *), void *arg, const char *name,
                     uint32_t stackBytes, uint32_t priority, int core);

// Deletes the calling task.
inline void taskExit() { vTaskDelete(NULL); }

// Lets a ready task of the same priority on this core run now.
inline void yield() { taskYIELD(); }

inline int coreId() { return xPortGetCoreID(); }

// At least one tick for any non-zero request: a zero-tick delay would not
// yield.
inline void sleepMs(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (!ticks && ms) {
    ticks = 1;
  }
  vTaskDelay(ticks);
}

// Latched event between tasks: give() sets, take() consumes everything
// pending. Several gives before a take count once.
class Signal {
public:
  Signal() : sem(xSemaphoreCreateBinary()) {}
  void give() { xSemaphoreGive(sem); }
  bool take(uint32_t timeoutMs) {
    const TickType_t ticks = timeoutMs == WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xSemaphoreTake(sem, ticks) == pdTRUE;
  }
  void clear() { xSemaphoreTake(sem, 0); }

private:
  SemaphoreHandle_t sem;
};

// One process-wide lock, safe across cores. Keep sections short.
namespace detail {
inline portMUX_TYPE &mux() {
  static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
  return m;
}
}

class CriticalSection {
public:
  CriticalSection() { portENTER_CRITICAL(&detail::mux()); }
  ~CriticalSection() { portEXIT_CRITICAL(&detail::mux()); }
};

} // namespace hal

#endif // HAL_ESP_RTOS_H
