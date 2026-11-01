// Raspberry Pi Pico 2 (RP2350). GPIO 12..19 are kept free for the HSTX video
// output. Buttons to ground, internal pull-ups. Video stays null until the
// DVI backend exists.
#ifndef HAL_BOARD_PICO2_H
#define HAL_BOARD_PICO2_H

#ifndef HAL_VIDEO_CHOSEN
#define HAL_VIDEO_NULL
#endif
#ifndef HAL_AUDIO_CHOSEN
#define HAL_AUDIO_NULL
#endif
#ifndef BTN_START_PIN
#define BTN_START_PIN   6
#endif
#ifndef BTN_LEFT_PIN
#define BTN_LEFT_PIN    2
#endif
#ifndef BTN_RIGHT_PIN
#define BTN_RIGHT_PIN   3
#endif
#ifndef BTN_DOWN_PIN
#define BTN_DOWN_PIN    4
#endif
#ifndef BTN_UP_PIN
#define BTN_UP_PIN      5
#endif
#ifndef BTN_FIRE_PIN
#define BTN_FIRE_PIN    7
#endif
#ifndef AUDIO_PWM_PIN
#define AUDIO_PWM_PIN   20
#endif

#endif // HAL_BOARD_PICO2_H
