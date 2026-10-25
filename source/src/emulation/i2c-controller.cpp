#include "i2c-controller.h"
#include <stdio.h>

#ifdef GALAGINO_CONTROLLER

// port0 by default
#ifndef GALAGINO_CONTROLLER_USE_I2C_PORT1
  #define I2C_PORT 0
#else
  #define I2C_PORT 1
#endif

void ControllerI2C::scan(uint8_t want) {
  printf("Scanning...\n");

  disable();
  dead=true;
  for (uint8_t addr = 0x08; addr<0x70; addr++) {
    if (hal::I2c::probe(I2C_PORT, addr)) {
      printf("Addr=0x%0x  (0) found device\n", addr);
      if (addr == want) {
        dead=false;
        enable();
      }
    }
  }
}

void ControllerI2C::setup() {
  const bool done = hal::I2c::init(I2C_PORT, GALAGINO_CONTROLLER_SDA, GALAGINO_CONTROLLER_SCL, 400000);
  printf("controller: init=%d SDA=%d SCL=%d Clock=%d\n",
    done, GALAGINO_CONTROLLER_SDA, GALAGINO_CONTROLLER_SCL,
    (int)hal::I2c::clockHz(I2C_PORT));

  const bool present = hal::I2c::probe(I2C_PORT, GALAGINO_CONTROLLER_ADDR);
  printf("Transmission: err=%d\n", present ? 0 : 1);
  lastUpdate = 0;
  lastValue = 0;
  //scan(GALAGINO_CONTROLLER_ADDR);
  enable();
}

void ControllerI2C::enable() {
  const bool present = hal::I2c::probe(I2C_PORT, GALAGINO_CONTROLLER_ADDR);

  printf("Addr=0x%0x err=%d\n", GALAGINO_CONTROLLER_ADDR, present ? 0 : 1);
  dead = !present;
  if (dead) {
    printf("ControllerI2C::enable (dead) \n");
    return;
  }

  enabled = true;
  printf("ControllerI2C::enable\n");
}

void ControllerI2C::disable() {
  enabled = false;
  printf("ControllerI2C::disable\n");
}

#ifndef CONTROLLER_UPDATE_MILLIS
#define CONTROLLER_UPDATE_MILLIS 33
#endif

unsigned int ControllerI2C::getInput() {

  unsigned long now = hal::millis();
  if (!enabled || now - lastUpdate < CONTROLLER_UPDATE_MILLIS) {
    // i2c updated only every 33ms (2 frames)
    return lastValue;
  } else {
    lastUpdate = now;
    uint8_t buf[2] = {0, 0};
    hal::I2c::read(I2C_PORT, GALAGINO_CONTROLLER_ADDR, buf, 2);
    lastValue = buf[0] | (buf[1] << 8);
  }
  return lastValue;
}

#endif
