#include "i2c.h"

#include <stdio.h>
#include <driver/i2c_master.h>

namespace hal {

namespace {

const int PORTS = 2;
const int TIMEOUT_MS = 10;
const int MAX_DEVICES = 4;  // per port: controller (0x09), codec (0x18)

struct Device {
  uint8_t addr;
  i2c_master_dev_handle_t handle;
};

struct Bus {
  i2c_master_bus_handle_t handle;
  uint32_t hz;
  Device devices[MAX_DEVICES];
};
Bus buses[PORTS];
bool tableFullReported;

// Device handles are created on first use of an address.
i2c_master_dev_handle_t device(int port, uint8_t addr) {
  Bus &b = buses[port];
  if (!b.handle) {
    return nullptr;
  }

  int i = 0;
  for (; i < MAX_DEVICES && b.devices[i].handle; i++) {
    if (b.devices[i].addr == addr) {
      return b.devices[i].handle;
    }
  }

  if (i == MAX_DEVICES) {
    if (!tableFullReported) {
      printf("I2C: more than %d devices on port %d\n", MAX_DEVICES, port);
      tableFullReported = true;
    }
    return nullptr;
  }

  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = addr;
  dc.scl_speed_hz = b.hz;
  if (i2c_master_bus_add_device(b.handle, &dc, &b.devices[i].handle) != ESP_OK) {
    b.devices[i].handle = nullptr;
    return nullptr;
  }
  b.devices[i].addr = addr;
  return b.devices[i].handle;
}

} // namespace

bool I2c::init(int port, int sda, int scl, uint32_t hz) {
  if (port < 0 || port >= PORTS) {
    return false;
  }
  if (buses[port].handle) {
    return true;
  }
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = (i2c_port_num_t)port;
  cfg.sda_io_num = (gpio_num_t)sda;
  cfg.scl_io_num = (gpio_num_t)scl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&cfg, &buses[port].handle) != ESP_OK) {
    return false;
  }
  buses[port].hz = hz;
  return true;
}

uint32_t I2c::clockHz(int port) {
  return buses[port].hz;
}

bool I2c::probe(int port, uint8_t addr) {
  return buses[port].handle && i2c_master_probe(buses[port].handle, addr, TIMEOUT_MS) == ESP_OK;
}

bool I2c::write(int port, uint8_t addr, const uint8_t *data, uint32_t len) {
  i2c_master_dev_handle_t d = device(port, addr);
  return d && i2c_master_transmit(d, data, len, TIMEOUT_MS) == ESP_OK;
}

uint32_t I2c::read(int port, uint8_t addr, uint8_t *data, uint32_t len) {
  i2c_master_dev_handle_t d = device(port, addr);
  return (d && i2c_master_receive(d, data, len, TIMEOUT_MS) == ESP_OK) ? len : 0;
}

bool I2c::writeRead(int port, uint8_t addr, const uint8_t *w, uint32_t wlen,
                    uint8_t *r, uint32_t rlen) {
  i2c_master_dev_handle_t d = device(port, addr);
  return d && i2c_master_transmit_receive(d, w, wlen, r, rlen, TIMEOUT_MS) == ESP_OK;
}

} // namespace hal

