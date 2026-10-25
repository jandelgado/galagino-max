#ifndef HAL_HOST_I2C_H
#define HAL_HOST_I2C_H

#include <stdint.h>
#include <string.h>

namespace hal {
namespace host {

// Fake bus: two ports, a device is an address that answers reads with fixed
// bytes.
struct I2cDevice {
  bool present;
  uint8_t reply[16];
  uint8_t replyLen;
  uint32_t reads;
};

inline I2cDevice &_i2cDevice(int port, uint8_t addr) {
  static I2cDevice devices[2][128];
  return devices[port & 1][addr & 0x7f];
}

inline void i2cReset() {
  for (int port = 0; port < 2; port++) {
    for (int addr = 0; addr < 128; addr++) {
      memset(&_i2cDevice(port, (uint8_t)addr), 0, sizeof(I2cDevice));
    }
  }
}

inline void i2cAttach(int port, uint8_t addr, const uint8_t *reply, uint8_t len) {
  I2cDevice &d = _i2cDevice(port, addr);
  d.present = true;
  d.replyLen = len > sizeof(d.reply) ? sizeof(d.reply) : len;
  memcpy(d.reply, reply, d.replyLen);
}

inline uint32_t i2cReads(int port, uint8_t addr) { return _i2cDevice(port, addr).reads; }

} // namespace host

class I2c {
public:
  static bool init(int, int, int, uint32_t) { return true; }
  static uint32_t clockHz(int) { return 400000; }
  static bool probe(int port, uint8_t addr) { return host::_i2cDevice(port, addr).present; }
  static bool write(int port, uint8_t addr, const uint8_t *, uint32_t) {
    return host::_i2cDevice(port, addr).present;
  }
  static uint32_t read(int port, uint8_t addr, uint8_t *data, uint32_t len) {
    host::I2cDevice &d = host::_i2cDevice(port, addr);
    if (!d.present) {
      return 0;
    }
    d.reads++;
    const uint32_t n = len < d.replyLen ? len : d.replyLen;
    memcpy(data, d.reply, n);
    return n;
  }
  static bool writeRead(int port, uint8_t addr, const uint8_t *, uint32_t, uint8_t *r, uint32_t rlen) {
    return read(port, addr, r, rlen) == rlen;
  }
};

} // namespace hal

#endif // HAL_HOST_I2C_H
