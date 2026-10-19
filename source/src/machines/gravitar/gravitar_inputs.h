#ifndef GRAVITAR_INPUTS_H
#define GRAVITAR_INPUTS_H

#include <stdint.h>

// Gravitar reads IN3 (0x8000) and IN4 (0x8800), active low (MAME
// INPUT_PORTS_START(gravitar)). MAME names the IN3 buttons only BUTTON1-3;
// the ROM (traced in test_gravitar_boot) uses them as below.
enum : uint8_t {
  GV_SHIELD = 0x01, // BUTTON3: shield / tractor beam
  GV_FIRE = 0x02,   // BUTTON1
  GV_RIGHT = 0x04,
  GV_LEFT = 0x08,
  GV_THRUST = 0x10, // BUTTON2
  GV_START1 = 0x20, // IN4
};

// needs BUTTON_* from input.h; k = buttons. Stick up = thrust, down or
// B = shield/tractor, FIRE or A = fire.
static inline uint8_t gravitar_in3(unsigned int k) {
  uint8_t v = 0xff;
  if (k & BUTTON_RIGHT) v &= ~GV_RIGHT;
  if (k & BUTTON_LEFT) v &= ~GV_LEFT;
  if (k & (BUTTON_FIRE | BUTTON_A)) v &= ~GV_FIRE;
  if (k & BUTTON_UP) v &= ~GV_THRUST;
  if (k & (BUTTON_DOWN | BUTTON_B)) v &= ~GV_SHIELD;
  return v;
}

static inline uint8_t gravitar_in4(unsigned int k) {
  return (k & BUTTON_START) ? uint8_t(0xff & ~GV_START1) : 0xff;
}

#endif
