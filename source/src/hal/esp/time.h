#ifndef HAL_ESP_TIME_H
#define HAL_ESP_TIME_H

#include <stdint.h>
#include <esp_timer.h>

namespace hal {

// 32 bit, wrap like Arduino: callers compare with unsigned differences.
inline uint32_t micros() { return (uint32_t)esp_timer_get_time(); }
inline uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

} // namespace hal

#endif // HAL_ESP_TIME_H
