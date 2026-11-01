#ifndef HAL_RP2350_TIME_H
#define HAL_RP2350_TIME_H

#include <stdint.h>

// No SDK header in the HAL headers: pico register headers define macros
// (ROM_BASE, SRAM_SIZE, ...) that collide with names in the machines. The
// calls are out of line (time.cpp).
namespace hal {

// 32 bit, wrap like Arduino: callers compare with unsigned differences.
uint32_t micros();
uint32_t millis();

} // namespace hal

#endif // HAL_RP2350_TIME_H
