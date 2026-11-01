#ifndef HAL_RP2350_RTOS_H
#define HAL_RP2350_RTOS_H

#include <stdint.h>

// FreeRTOS SMP behind out of line calls (rtos.cpp): its headers pull in the
// pico SDK register macros, see time.h.
namespace hal {

// The video core runs the main task (display, audio refill); the emulation
// task runs on the other one.
static const int VIDEO_CORE = 0;
static const int EMULATION_CORE = 1 - VIDEO_CORE;

static const uint32_t WAIT_FOREVER = 0xFFFFFFFFu;

typedef void *TaskHandle;

// Returns NULL if the task could not be created. Pinned to the core.
TaskHandle taskSpawn(void (*fn)(void *), void *arg, const char *name,
                     uint32_t stackBytes, uint32_t priority, int core);

// Deletes the calling task.
void taskExit();

// Lets a ready task of the same priority on this core run now.
void yield();

int coreId();

// At least one tick for any non-zero request: a zero-tick delay would not
// yield.
void sleepMs(uint32_t ms);

// Latched event between tasks: give() sets, take() consumes everything
// pending. Several gives before a take count once.
class Signal {
public:
  Signal();
  void give();
  bool take(uint32_t timeoutMs);
  void clear();

private:
  void *sem;
};

// One process-wide lock, safe across cores. Keep sections short.
class CriticalSection {
public:
  CriticalSection();
  ~CriticalSection();
};

} // namespace hal

#endif // HAL_RP2350_RTOS_H
