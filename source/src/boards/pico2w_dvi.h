// Raspberry Pi Pico 2 W with a DVI connector soldered to HSTX GPIO 12..19,
// in the Pico DVI Sock order (pico-examples dvi_out_hstx_encoder): 12/13 D0,
// 14/15 CK, 16/17 D2, 18/19 D1 (P on the even pin). TV over HDMI, sound
// over the same cable. One input source: a Bluetooth pad (8BitDo SN30 Pro /
// Pro 2) over the CYW43; -D NUNCHUCK_INPUT or -D GALAGINO_CONTROLLER: that
// device on I2C0 GPIO 20/21 instead; -D GPIO_BUTTONS: buttons as
// boards/pico2.h.
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
#if !defined(GPIO_BUTTONS) && !defined(GALAGINO_CONTROLLER) && !defined(NUNCHUCK_INPUT)
#define HAL_GAMEPAD_BT
#endif
#ifndef NUNCHUCK_SDA
#define NUNCHUCK_SDA    20
#endif
#ifndef NUNCHUCK_SCL
#define NUNCHUCK_SCL    21
#endif
#ifndef NUNCHUCK_MOVE_THRESHOLD
#define NUNCHUCK_MOVE_THRESHOLD 30 // stick dead zone
#endif
#ifndef NUNCHUCK_UPDATE_MS
#define NUNCHUCK_UPDATE_MS 16  // every frame: input lag 16-33 ms
#endif
#ifndef GALAGINO_CONTROLLER_SDA
#define GALAGINO_CONTROLLER_SDA 20
#endif
#ifndef GALAGINO_CONTROLLER_SCL
#define GALAGINO_CONTROLLER_SCL 21
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
