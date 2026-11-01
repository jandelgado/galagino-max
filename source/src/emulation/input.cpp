#include "input.h"
#include <stdio.h>
#include <string.h>
#include "../hal/hal.h"

void Input::init(bool SingleMachine) {
  singleMachine = SingleMachine;
#ifndef NUNCHUCK_INPUT
  #ifdef GALAGINO_CONTROLLER
  Controller.setup();
  #else
  hal::pinInputPullup(BTN_START_PIN);
  #ifdef BTN_COIN_PIN
    hal::pinInputPullup(BTN_COIN_PIN);
  #endif
  hal::pinInputPullup(BTN_LEFT_PIN);
  hal::pinInputPullup(BTN_RIGHT_PIN);
  hal::pinInputPullup(BTN_DOWN_PIN);
  hal::pinInputPullup(BTN_UP_PIN);
  hal::pinInputPullup(BTN_FIRE_PIN);
  #endif
#else
  nunchuck.setup();
#endif

  unsigned int inputs = buttons_get();
  if (inputs & BUTTON_FIRE) {
    printf("Demo Sounds switched off\n");
    switchDemoSoundsOff = 1;
    firePressedAtStart = 1;
  }
  #ifdef SILENT_ATTRACT_MODE
  printf("Silent attract mode\n");
  switchDemoSoundsOff = 1;
  firePressedAtStart = 1;
  #endif
}

bool Input::demoSoundsOff() {
  return switchDemoSoundsOff;
}

void Input::enable() {
#ifdef NUNCHUCK_INPUT
  nunchuck.enable();
#endif
#ifdef GALAGINO_CONTROLLER
  Controller.enable();
#endif
}

void Input::disable() {
#ifdef NUNCHUCK_INPUT
  nunchuck.disable();
  hal::sleepMs(100);
#endif
#ifdef GALAGINO_CONTROLLER
  Controller.disable();
  hal::sleepMs(100);
#endif
}
unsigned int Input::buttons_get(void) {
  // galagino can be compiled without coin button. This will then
  // be implemented by the start button. Whenever the start button 
  // is pressed, a virtual coin button will be sent first 
  unsigned int input_states = 0;
#if defined(GALAGINO_CONTROLLER)
  input_states = Controller.getInput();
#elif defined(NUNCHUCK_INPUT)
  input_states = nunchuck.getInput();  
#else
  #ifdef BTN_COIN_PIN
    input_states = (!hal::pinRead(BTN_COIN_PIN)) ? BUTTON_EXTRA : 0;
  #else
    input_states = (!hal::pinRead(BTN_START_PIN)) ? BUTTON_EXTRA : 0;
  #endif
  input_states |=
    (hal::pinRead(BTN_LEFT_PIN) ? 0 : BUTTON_LEFT) |
    (hal::pinRead(BTN_RIGHT_PIN) ? 0 : BUTTON_RIGHT) |
    (hal::pinRead(BTN_UP_PIN) ? 0 : BUTTON_UP) |
    (hal::pinRead(BTN_DOWN_PIN) ? 0 : BUTTON_DOWN) |
    (hal::pinRead(BTN_FIRE_PIN) ? 0 : BUTTON_FIRE);
#endif
  
  unsigned int startAndCoinState = 0;
#ifdef BTN_COIN_PIN 
  // there is a coin pin -> coin and start work normal
  startAndCoinState = (hal::pinRead(BTN_START_PIN) ? 0 : BUTTON_START) |
    (hal::pinRead(BTN_COIN_PIN) ? 0 : BUTTON_COIN);
#elif defined(GALAGINO_CONTROLLER)
  // Coin and Start work normal - no virtual Coin
#else
  switch(virtual_coin_state)  {
    case 0:  // idle state
      if(input_states & BUTTON_EXTRA) {
        virtual_coin_state = 1;   // virtual coin pressed
        virtual_coin_timer = hal::millis();
      }
      break;
    case 1:  // start was just pressed
      // check if 100 milliseconds have passed
      if(hal::millis() - virtual_coin_timer > 100) {
        virtual_coin_state = 2;   // virtual coin released
        virtual_coin_timer = hal::millis();        
      }
      break;
    case 2:  // virtual coin was released
      // check if 500 milliseconds have passed
      if(hal::millis() - virtual_coin_timer > 500) {
        virtual_coin_state = 3;   // pause between virtual coin an start ended
        virtual_coin_timer = hal::millis();        
      }
      break;
    case 3:  // pause ended
      // check if 100 milliseconds have passed
      if(hal::millis() - virtual_coin_timer > 100) {
        virtual_coin_state = 4;   // virtual start ended
        virtual_coin_timer = hal::millis();        
      }
      break;
    case 4:  // virtual start has ended
      // check if start button is actually still pressed
      if(!(input_states & BUTTON_EXTRA))
        virtual_coin_state = 0;   // button has been released, return to idle
      break;
  }
  startAndCoinState = ((virtual_coin_state != 1) ? 0 : BUTTON_COIN) | (((virtual_coin_state != 3) && (virtual_coin_state != 4)) ? 0 : BUTTON_START); 
#endif

  // volume control
  if ((input_states & BUTTON_EXTRA) && _volume_callback) {
    _volume_callback(input_states & BUTTON_UP, input_states & BUTTON_DOWN);
    
    if ((input_states & BUTTON_UP) | (input_states & BUTTON_DOWN))
      reset_timer = 0;
  }

  if (!singleMachine) {
    bool buttonExtraRisingEdge = (input_states & BUTTON_EXTRA) && !(input_states_last & BUTTON_EXTRA); 
    bool buttonUpRisingEdge = (input_states & BUTTON_UP) && !(input_states_last & BUTTON_UP); 
    bool buttonDownRisingEdge = (input_states & BUTTON_DOWN) && !(input_states_last & BUTTON_DOWN); 

    // joystick up/down (menu) or extra disables attract mode
    if (buttonUpRisingEdge | buttonDownRisingEdge | buttonExtraRisingEdge ) {
      if (_doAttractReset_callback)
        _doAttractReset_callback();  
      //printf("risingEdge AttractReset\n");
    }

    // reset control
    if(input_states & BUTTON_MENU) {
      if (_doReset_callback)
        _doReset_callback();
    }

    // reset control
    if(input_states & BUTTON_EXTRA) {
      if(!reset_timer)
        reset_timer = hal::millis();
    
      // reset if coin (or start if no coin is configured) is held for more than 3 seconds
      if(hal::millis() - reset_timer > 3000) {
        reset_timer = hal::millis();
        if (_doReset_callback)
          _doReset_callback();
      }
    }
    else
      reset_timer = 0;

    input_states_last = input_states;
  }

  if (firePressedAtStart && input_states & BUTTON_FIRE) {
    printf("Wait for release fire button...\n");
    input_states = 0;
  }
  else {
    firePressedAtStart = false;
  }

  fire_raw_state = input_states & BUTTON_FIRE;

  return input_states | startAndCoinState;
}

Input &Input::onVolumeUpDown(THandlerVolume fn) {
  _volume_callback = fn;
  return *this;
}

Input &Input::onDoReset(THandlerDoReset fn) {
  _doReset_callback = fn;
  return *this;
}

Input &Input::onDoAttractReset(THandlerDoAttractReset fn) {
  _doAttractReset_callback = fn;
  return *this;
}

// galagino3 - used by Tutankham - needs fixing
bool Input::button_y_pressed(void) {
  return false;
}
