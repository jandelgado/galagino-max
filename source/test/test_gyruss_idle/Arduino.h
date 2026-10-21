#pragma once
// host stub: machineBase.h includes Arduino.h; gyruss also needs the
// FreeRTOS calls of its audio task (never started here)
#include <functional>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void *TaskHandle_t;
#define ARDUINO_RUNNING_CORE 0
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(int) {}
inline void disableCore0WDT() {}
inline void disableCore1WDT() {}
inline void enableCore0WDT() {}
inline void enableCore1WDT() {}
inline int xPortGetCoreID() { return 0; }
inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, int, void *, int, TaskHandle_t *h, int) {
  *h = nullptr;
  return 0;
}
