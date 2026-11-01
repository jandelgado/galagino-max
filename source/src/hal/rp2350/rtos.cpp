#ifdef HAL_RP2350
#include "rtos.h"
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

namespace hal {

TaskHandle taskSpawn(void (*fn)(void *), void *arg, const char *name,
                     uint32_t stackBytes, uint32_t priority, int core) {
  TaskHandle_t handle = NULL;
  if (xTaskCreateAffinitySet(fn, name, stackBytes / sizeof(StackType_t), arg, priority,
                             (UBaseType_t)1 << core, &handle) != pdPASS) {
    return NULL;
  }
  return handle;
}

void taskExit() { vTaskDelete(NULL); }

void yield() { taskYIELD(); }

int coreId() { return portGET_CORE_ID(); }

void sleepMs(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (!ticks && ms) {
    ticks = 1;
  }
  vTaskDelay(ticks);
}

Signal::Signal() : sem(xSemaphoreCreateBinary()) {}

void Signal::give() { xSemaphoreGive((SemaphoreHandle_t)sem); }

bool Signal::take(uint32_t timeoutMs) {
  const TickType_t ticks = timeoutMs == WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
  return xSemaphoreTake((SemaphoreHandle_t)sem, ticks) == pdTRUE;
}

void Signal::clear() { xSemaphoreTake((SemaphoreHandle_t)sem, 0); }

CriticalSection::CriticalSection() { taskENTER_CRITICAL(); }
CriticalSection::~CriticalSection() { taskEXIT_CRITICAL(); }

} // namespace hal
#endif // HAL_RP2350
