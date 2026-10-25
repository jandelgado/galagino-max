#ifndef _NUNCHUCK_H_
#define _NUNCHUCK_H_

// Nintendo Nunchuk through the Arduino NintendoExtensionCtrl library, which needs
// Arduino Wire: Arduino-ESP32 builds only. Not on hal::I2c: the library owns
// the bus.

#include "../../config.h"

#ifdef NUNCHUCK_INPUT

// The library behind it (nunchuck.cpp) needs to be installed:
// https://github.com/dmadison/NintendoExtensionCtrl
// Not included here: it pulls in Arduino.h, which must not follow Z80.h.

// a total of 7 button is needed for most games
#define BUTTON_LEFT  0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP    0x04
#define BUTTON_DOWN  0x08
#define BUTTON_FIRE  0x10
#define BUTTON_START 0x20
#define BUTTON_COIN  0x40
#define BUTTON_EXTRA 0x80

class Nunchuck {
public:
  void setup();
  void enable();
  void disable();
  unsigned int getInput();
  bool connected();
 
private:
  struct Impl;
  Impl *impl = nullptr;
  bool enabled;
  unsigned long timeout;
  unsigned int lastValue;
};

#endif

#endif //_NUNCHUCK_H_
