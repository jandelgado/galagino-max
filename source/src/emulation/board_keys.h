#ifndef BOARD_KEYS_H
#define BOARD_KEYS_H

// galagino button bits (same values as emulation/input.h, which needs Arduino.h)
static const unsigned int BOARD_KEY_LEFT = 0x01, BOARD_KEY_RIGHT = 0x02, BOARD_KEY_UP = 0x04;
static const unsigned int BOARD_KEY_DOWN = 0x08, BOARD_KEY_FIRE = 0x10, BOARD_KEY_START = 0x20;
static const unsigned int BOARD_KEY_COIN = 0x40, BOARD_KEY_EXTRA = 0x80;
static const unsigned int BOARD_KEY_A = 0x100, BOARD_KEY_B = 0x200, BOARD_KEY_X = 0x400;
static const unsigned int BOARD_KEY_Y = 0x800;

#endif
