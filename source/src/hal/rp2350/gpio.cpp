#ifdef HAL_RP2350
#include "gpio.h"
#include <hardware/gpio.h>

namespace hal {

void pinOutput(int pin, bool level) {
  gpio_init(pin);
  gpio_put(pin, level);
  gpio_set_dir(pin, GPIO_OUT);
}

void pinInputPullup(int pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);
}

void pinWrite(int pin, bool level) { gpio_put(pin, level); }

bool pinRead(int pin) { return gpio_get(pin); }

} // namespace hal
#endif // HAL_RP2350
