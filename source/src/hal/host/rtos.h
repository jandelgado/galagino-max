#ifndef HAL_HOST_RTOS_H
#define HAL_HOST_RTOS_H

#include <stdint.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace hal {

static const int VIDEO_CORE = 0;
static const int EMULATION_CORE = 1;
static const uint32_t WAIT_FOREVER = 0xFFFFFFFFu;

typedef void *TaskHandle;

// Tasks are not run on the host: tests call machine code directly.
inline TaskHandle taskSpawn(void (*)(void *), void *, const char *, uint32_t, uint32_t, int) {
  return nullptr;
}
inline void taskExit() {}
// Host tests run machine code as if on the emulation core.
inline int coreId() { return EMULATION_CORE; }
inline void sleepMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class Signal {
public:
  void give() {
    std::lock_guard<std::mutex> lock(m);
    pending = true;
    cv.notify_one();
  }
  bool take(uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lock(m);
    if (timeoutMs == WAIT_FOREVER) {
      cv.wait(lock, [this] { return pending; });
    } else if (!cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return pending; })) {
      return false;
    }
    pending = false;
    return true;
  }
  void clear() {
    std::lock_guard<std::mutex> lock(m);
    pending = false;
  }

private:
  std::mutex m;
  std::condition_variable cv;
  bool pending = false;
};

class CriticalSection {};

} // namespace hal

#endif // HAL_HOST_RTOS_H
