#pragma once

#include "../config.h"

#ifdef GALAGINO_CONTROLLER
#include "../hal/hal.h"

// The controller sends a 16 bit word of BUTTON_* masks (buttons.h).

#ifndef GALAGINO_CONTROLLER_ADDR
#define GALAGINO_CONTROLLER_ADDR 0x09
#endif
#ifndef GALAGINO_CONTROLLER_SDA
#define GALAGINO_CONTROLLER_SDA 27
#endif
#ifndef GALAGINO_CONTROLLER_SCL
#define GALAGINO_CONTROLLER_SCL 22
#endif

class ControllerI2C {
public:
  void setup();
  void enable();
  void disable();
  void scan(uint8_t);
  unsigned int getInput();

private:
  bool enabled;
  bool dead;
  unsigned long lastUpdate;
  unsigned int lastValue;
};

#endif
