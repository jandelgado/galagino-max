#ifndef HAL_ESP_GPIO_H
#define HAL_ESP_GPIO_H

#include <driver/gpio.h>
#include <esp_rom_gpio.h>

namespace hal {

inline void pinOutput(int pin, bool level) {
  esp_rom_gpio_pad_select_gpio(pin);
  gpio_set_level((gpio_num_t)pin, level);  // level first: no glitch
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
}

inline void pinInputPullup(int pin) {
  esp_rom_gpio_pad_select_gpio(pin);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
}

inline void pinWrite(int pin, bool level) { gpio_set_level((gpio_num_t)pin, level); }
inline bool pinRead(int pin) { return gpio_get_level((gpio_num_t)pin) != 0; }

} // namespace hal

#endif // HAL_ESP_GPIO_H
