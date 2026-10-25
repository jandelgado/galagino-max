#ifndef HAL_NUNCHUCK_H
#define HAL_NUNCHUCK_H

// Nunchuk input (NUNCHUCK_INPUT builds). The only implementation is the
// Arduino-ESP32 one: it wraps a third party Arduino library.

#if defined(HAL_ESP) && defined(ARDUINO)
#include "esp/nunchuck.h"
#else
#error "NUNCHUCK_INPUT is supported on Arduino-ESP32 builds only"
#endif

#endif // HAL_NUNCHUCK_H
