#ifndef HAL_HOST_I2C_H
#define HAL_HOST_I2C_H

#include <stdint.h>
#include <string.h>

namespace hal {
namespace host {

// Fake bus: two ports, a device is an address that answers reads with fixed
// bytes, or a register file (write [reg, value] stores, writeRead [reg]
// returns it).
struct I2cDevice {
  bool present;
  bool registers;
  uint8_t reply[16];
  uint8_t replyLen;
  uint32_t reads;
  uint8_t regs[256];
};

// Register writes of register-file devices, in order: reg << 8 | value.
inline uint16_t *_i2cLog() {
  static uint16_t log[256];
  return log;
}
inline uint32_t &_i2cLogLen() {
  static uint32_t n = 0;
  return n;
}

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
  _i2cLogLen() = 0;
}

inline void i2cAttach(int port, uint8_t addr, const uint8_t *reply, uint8_t len) {
  I2cDevice &d = _i2cDevice(port, addr);
  d.present = true;
  d.replyLen = len > sizeof(d.reply) ? sizeof(d.reply) : len;
  memcpy(d.reply, reply, d.replyLen);
}

// Register-file device, all registers 0.
inline void i2cAttachRegisters(int port, uint8_t addr) {
  I2cDevice &d = _i2cDevice(port, addr);
  d.present = true;
  d.registers = true;
}

inline uint32_t i2cReads(int port, uint8_t addr) { return _i2cDevice(port, addr).reads; }
inline const uint16_t *i2cWriteLog() { return _i2cLog(); }
inline uint32_t i2cWriteLogLen() { return _i2cLogLen(); }

} // namespace host

class I2c {
public:
  static bool init(int, int, int, uint32_t) { return true; }
  static uint32_t clockHz(int) { return 400000; }
  static bool probe(int port, uint8_t addr) { return host::_i2cDevice(port, addr).present; }
  static bool write(int port, uint8_t addr, const uint8_t *data, uint32_t len) {
    host::I2cDevice &d = host::_i2cDevice(port, addr);
    if (d.registers && len == 2) {
      d.regs[data[0]] = data[1];
      if (host::_i2cLogLen() < 256) {
        host::_i2cLog()[host::_i2cLogLen()++] = (uint16_t)(data[0] << 8 | data[1]);
      }
    }
    return d.present;
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
  static bool writeRead(int port, uint8_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen) {
    host::I2cDevice &d = host::_i2cDevice(port, addr);
    if (d.registers && wlen == 1 && rlen == 1) {
      d.reads++;
      r[0] = d.regs[w[0]];
      return true;
    }
    return read(port, addr, r, rlen) == rlen;
  }
};

} // namespace hal

#endif // HAL_HOST_I2C_H
