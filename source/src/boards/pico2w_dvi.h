// Raspberry Pi Pico 2 W with a DVI connector soldered to HSTX GPIO 12..19,
// in the Pico DVI Sock order (pico-examples dvi_out_hstx_encoder): 12/13 D0,
// 14/15 CK, 16/17 D2, 18/19 D1 (P on the even pin). TV over HDMI, sound
// over the same cable. Buttons as boards/pico2.h; the CYW43 pins (23, 24, 25,
// 29) stay untouched.
#ifndef HAL_BOARD_PICO2W_DVI_H
#define HAL_BOARD_PICO2W_DVI_H

#ifndef HAL_VIDEO_CHOSEN
#define HAL_VIDEO_DVI_HSTX
#endif
#ifndef HAL_AUDIO_CHOSEN
#define HAL_AUDIO_DVI
#endif
#ifndef HSTX_CK_PIN
#define HSTX_CK_PIN     14
#endif
#ifndef HSTX_D0_PIN
#define HSTX_D0_PIN     12
#endif
#ifndef HSTX_D1_PIN
#define HSTX_D1_PIN     18
#endif
#ifndef HSTX_D2_PIN
#define HSTX_D2_PIN     16
#endif
// 1: 1x centered, 2: 2x wide with scaled rows
#ifndef DVI_SCALE
#define DVI_SCALE       2
#endif
// 640x480 60 Hz: 25.2 MHz pixel clock
#ifndef GALAGINO_CLOCK_KHZ
#define GALAGINO_CLOCK_KHZ 252000
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

#endif // HAL_BOARD_PICO2W_DVI_H
