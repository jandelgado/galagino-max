#ifdef HAL_RP2350
#include "time.h"
#include <pico/time.h>

namespace hal {

uint32_t micros() { return time_us_32(); }
uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }

} // namespace hal
#endif // HAL_RP2350
