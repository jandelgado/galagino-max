#ifndef INPUT_H
#define INPUT_H

#include <functional>
#include "../hal/hal.h"
#include "../config.h"
#include "../input/buttons.h"
#ifdef NUNCHUCK_INPUT
#include "../input/nunchuk.h"
#endif
#ifdef GALAGINO_CONTROLLER
#include "../input/galagino_controller.h"
#endif

class Input {
public:
  void init(bool SingleMachine);
  void enable();
  void disable();
  unsigned int buttons_get(void);
  bool button_y_pressed(void);
  unsigned int fire_raw(void) { return fire_raw_state; }
  bool demoSoundsOff();
#ifdef NUNCHUCK_INPUT
  bool nunchuckConnected() { return nunchuck.connected(); }
#endif

  typedef std::function<void(bool up, bool down)> THandlerVolume;
  Input& onVolumeUpDown(THandlerVolume fn);

  typedef std::function<void(void)> THandlerDoReset;
  Input& onDoReset(THandlerDoReset fn);

  typedef std::function<void(void)> THandlerDoAttractReset;
  Input& onDoAttractReset(THandlerDoAttractReset fn);

private:
  THandlerVolume _volume_callback;
  THandlerDoReset _doReset_callback;
  THandlerDoAttractReset _doAttractReset_callback;
  unsigned int input_states_last;
  int virtual_coin_state;
  unsigned long virtual_coin_timer;
  unsigned long reset_timer;
  bool singleMachine;
  bool switchDemoSoundsOff;
  bool firePressedAtStart;
  unsigned int fire_raw_state = 0;
#ifdef NUNCHUCK_INPUT
  Nunchuk nunchuck;
#endif
#ifdef GALAGINO_CONTROLLER
  ControllerI2C Controller;
#endif
};

#endif
