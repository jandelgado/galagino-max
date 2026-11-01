#ifdef HAL_RP2350
#include "i2c.h"
#include <hardware/gpio.h>
#include <hardware/i2c.h>

namespace hal {

namespace {

const uint32_t TIMEOUT_US = 20000;

struct Bus {
  bool started;
  uint32_t hz;
};
Bus buses[2];

i2c_inst_t *instance(int port) {
  return port == 0 ? i2c0 : port == 1 ? i2c1 : nullptr;
}

} // namespace

bool I2c::init(int port, int sda, int scl, uint32_t hz) {
  i2c_inst_t *i2c = instance(port);
  if (!i2c) {
    return false;
  }
  if (!buses[port].started) {
    buses[port].hz = i2c_init(i2c, hz);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    buses[port].started = true;
  }
  return true;
}

uint32_t I2c::clockHz(int port) {
  return port == 0 || port == 1 ? buses[port].hz : 0;
}

bool I2c::probe(int port, uint8_t addr) {
  i2c_inst_t *i2c = instance(port);
  uint8_t dummy;
  return i2c && i2c_read_timeout_us(i2c, addr, &dummy, 1, false, TIMEOUT_US) == 1;
}

bool I2c::write(int port, uint8_t addr, const uint8_t *data, uint32_t len) {
  i2c_inst_t *i2c = instance(port);
  return i2c && i2c_write_timeout_us(i2c, addr, data, len, false, TIMEOUT_US) == (int)len;
}

uint32_t I2c::read(int port, uint8_t addr, uint8_t *data, uint32_t len) {
  i2c_inst_t *i2c = instance(port);
  if (!i2c) {
    return 0;
  }
  const int n = i2c_read_timeout_us(i2c, addr, data, len, false, TIMEOUT_US);
  return n > 0 ? (uint32_t)n : 0;
}

bool I2c::writeRead(int port, uint8_t addr, const uint8_t *w, uint32_t wlen,
                    uint8_t *r, uint32_t rlen) {
  i2c_inst_t *i2c = instance(port);
  if (!i2c || i2c_write_timeout_us(i2c, addr, w, wlen, true, TIMEOUT_US) != (int)wlen) {
    return false;
  }
  return i2c_read_timeout_us(i2c, addr, r, rlen, false, TIMEOUT_US) == (int)rlen;
}

} // namespace hal
#endif // HAL_RP2350
