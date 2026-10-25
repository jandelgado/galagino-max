#ifndef HAL_H
#define HAL_H

// The only include portable code uses for hardware access. Exactly one
// backend per subsystem is compiled in, selected by the build:
// HAL_ESP, HAL_RP2350 or HAL_HOST. Contract: CONTRACT.md.

#include <stdint.h>
#include "hal_attr.h"

#if defined(HAL_ESP)
#include "esp/time.h"
#include "esp/gpio.h"
#include "esp/rtos.h"
#include "esp/watchdog.h"
#include "esp/led.h"
#include "esp/audio.h"
#include "esp/i2c.h"
#include "esp/sys.h"
#elif defined(HAL_RP2350)
#include "rp2350/time.h"
#include "rp2350/gpio.h"
#include "rp2350/rtos.h"
#include "rp2350/watchdog.h"
#include "rp2350/led.h"
#include "rp2350/audio.h"
#include "rp2350/i2c.h"
#include "rp2350/sys.h"
#elif defined(HAL_HOST)
#include "host/time.h"
#include "host/gpio.h"
#include "host/rtos.h"
#include "host/watchdog.h"
#include "host/led.h"
#include "host/audio.h"
#include "host/i2c.h"
#include "host/sys.h"
#else
#error "define one of HAL_ESP, HAL_RP2350, HAL_HOST"
#endif

#endif // HAL_H
