// Adafruit Feather RP2350 with HSTX port: an Adafruit DVI Sock on a ribbon
// cable at the 22-pin HSTX connector, TV over HDMI (silent until DVI audio).
// HSTX pairs per the Adafruit pinout: 12/13 D2, 14/15 CK, 16/17 D1, 18/19 D0
// (P on the even pin). Buttons to ground, internal pull-ups; STEMMA QT
// (2/3) stays free for a Nunchuk. Never used: 7 (red LED), 8 (PSRAM CS),
// 10/11 (HSTX connector), 21 (NeoPixel), 0/1 (UART).
#ifndef HAL_BOARD_FEATHER_RP2350_H
#define HAL_BOARD_FEATHER_RP2350_H

#ifndef HAL_VIDEO_CHOSEN
#define HAL_VIDEO_DVI_HSTX
#endif
#ifndef HAL_AUDIO_CHOSEN
#define HAL_AUDIO_NULL
#endif
#ifndef HSTX_CK_PIN
#define HSTX_CK_PIN     14
#endif
#ifndef HSTX_D0_PIN
#define HSTX_D0_PIN     18
#endif
#ifndef HSTX_D1_PIN
#define HSTX_D1_PIN     16
#endif
#ifndef HSTX_D2_PIN
#define HSTX_D2_PIN     12
#endif
// 1: 1x centered, 2: 2x wide with scaled rows
#ifndef DVI_SCALE
#define DVI_SCALE       2
#endif
// 640x480 60 Hz: 25.2 MHz pixel clock
#ifndef GALAGINO_CLOCK_KHZ
#define GALAGINO_CLOCK_KHZ 252000
#endif
#ifndef BTN_LEFT_PIN
#define BTN_LEFT_PIN    5
#endif
#ifndef BTN_RIGHT_PIN
#define BTN_RIGHT_PIN   6
#endif
#ifndef BTN_UP_PIN
#define BTN_UP_PIN      24
#endif
#ifndef BTN_DOWN_PIN
#define BTN_DOWN_PIN    25
#endif
#ifndef BTN_FIRE_PIN
#define BTN_FIRE_PIN    9
#endif
#ifndef BTN_START_PIN
#define BTN_START_PIN   26
#endif
#ifndef BTN_COIN_PIN
#define BTN_COIN_PIN    27
#endif

#endif // HAL_BOARD_FEATHER_RP2350_H
