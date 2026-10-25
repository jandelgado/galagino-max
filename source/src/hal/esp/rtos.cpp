#include "rtos.h"

namespace hal {

TaskHandle taskSpawn(void (*fn)(void *), void *arg, const char *name,
                     uint32_t stackBytes, uint32_t priority, int core) {
  TaskHandle handle = NULL;
  if (xTaskCreatePinnedToCore(fn, name, stackBytes, arg, priority, &handle, core) != pdPASS) {
    return NULL;
  }
  return handle;
}

} // namespace hal
