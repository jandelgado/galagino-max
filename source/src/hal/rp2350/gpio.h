#ifndef HAL_RP2350_GPIO_H
#define HAL_RP2350_GPIO_H

#include <stdint.h>

namespace hal {

// Out of line (gpio.cpp), see time.h.
void pinOutput(int pin, bool level);
void pinInputPullup(int pin);
void pinWrite(int pin, bool level);
bool pinRead(int pin);

} // namespace hal

#endif // HAL_RP2350_GPIO_H
