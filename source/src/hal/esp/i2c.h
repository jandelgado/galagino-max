#ifndef HAL_ESP_I2C_H
#define HAL_ESP_I2C_H

#include <stdint.h>

namespace hal {

// I2C master. Port numbers are the chip's I2C controllers (0, 1). 7 bit
// addresses. Transfers are bounded by the backend's bus timeout.
class I2c {
public:
  // Starts the bus; idempotent per port, so users sharing a bus (controller,
  // audio codec) each call it. False if the bus cannot be started.
  static bool init(int port, int sda, int scl, uint32_t hz);
  static uint32_t clockHz(int port);
  // Address acknowledges?
  static bool probe(int port, uint8_t addr);
  static bool write(int port, uint8_t addr, const uint8_t *data, uint32_t len);
  // Bytes actually read, up to len; 0 if the device does not answer.
  static uint32_t read(int port, uint8_t addr, uint8_t *data, uint32_t len);
  // Write, repeated start, read exactly rlen bytes.
  static bool writeRead(int port, uint8_t addr, const uint8_t *w, uint32_t wlen,
                        uint8_t *r, uint32_t rlen);
};

} // namespace hal

#endif // HAL_ESP_I2C_H
