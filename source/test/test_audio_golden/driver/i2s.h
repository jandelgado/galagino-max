#pragma once
// host stub of the ESP-IDF I2S driver: audio.cpp's transmit path only
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
typedef int i2s_port_t;
enum { I2S_NUM_0 = 0 };
#define portMAX_DELAY 0xffffffffu
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(4, 4, 4)  // as the pinned platform

esp_err_t i2s_write(i2s_port_t port, const void *src, size_t size, size_t *out, uint32_t ticks);
esp_err_t i2s_set_sample_rates(i2s_port_t port, uint32_t rate);
