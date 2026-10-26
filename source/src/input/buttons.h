#ifndef INPUT_BUTTONS_H
#define INPUT_BUTTONS_H

// Button masks of Input::buttons_get() and every input source.

// a total of 7 button is needed for most games
#define BUTTON_LEFT  0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP    0x04
#define BUTTON_DOWN  0x08
#define BUTTON_FIRE  0x10
#define BUTTON_START 0x20
#define BUTTON_COIN  0x40
#define BUTTON_EXTRA 0x80

#define BUTTON_A    0x0100
#define BUTTON_B    0x0200
#define BUTTON_X    0x0400
#define BUTTON_Y    0x0800
#define BUTTON_L1   0x1000
#define BUTTON_R1   0x2000

#define BUTTON_MENU 0x8000

#endif // INPUT_BUTTONS_H
