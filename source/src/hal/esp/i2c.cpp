#include "i2c.h"

// Arduino-ESP32 build: the bus is Arduino's Wire, so libraries that use Wire
// (Nunchuk) share it. The pure ESP-IDF build gets an IDF driver version.
#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>

namespace hal {

static TwoWire &bus(int port) {
  return port == 0 ? Wire : Wire1;
}

bool I2c::init(int port, int sda, int scl, uint32_t hz) {
  if (i2cIsInit(port)) {
    return true;
  }
  return bus(port).begin(sda, scl, hz);
}

uint32_t I2c::clockHz(int port) {
  return bus(port).getClock();
}

bool I2c::probe(int port, uint8_t addr) {
  TwoWire &w = bus(port);
  w.beginTransmission(addr);
  return w.endTransmission() == 0;
}

bool I2c::write(int port, uint8_t addr, const uint8_t *data, uint32_t len) {
  TwoWire &w = bus(port);
  w.beginTransmission(addr);
  w.write(data, len);
  return w.endTransmission() == 0;
}

uint32_t I2c::read(int port, uint8_t addr, uint8_t *data, uint32_t len) {
  TwoWire &w = bus(port);
  w.requestFrom(addr, (uint8_t)len);
  uint32_t count = 0;
  while (w.available()) {
    const int b = w.read();
    if (count < len) {
      data[count++] = (uint8_t)b;
    }
  }
  return count;
}

bool I2c::writeRead(int port, uint8_t addr, const uint8_t *w, uint32_t wlen,
                    uint8_t *r, uint32_t rlen) {
  TwoWire &bw = bus(port);
  bw.beginTransmission(addr);
  bw.write(w, wlen);
  if (bw.endTransmission(false) != 0) {
    return false;
  }
  return read(port, addr, r, rlen) == rlen;
}

} // namespace hal

#endif // ARDUINO
