// Shared by the CYD-style boards with a galagino controller (or a Nunchuk) on
// I2C: the stick and buttons come from it, START stays on GPIO 35.
#ifndef HAL_BOARD_CYD_CONTROLLER_H
#define HAL_BOARD_CYD_CONTROLLER_H

#ifndef HAL_VIDEO_CHOSEN
#define HAL_VIDEO_SPI_TFT
#endif
#ifndef HAL_AUDIO_CHOSEN
#define HAL_AUDIO_ESP_DAC
#endif
#ifndef TFT_DC
#define TFT_DC          2
#endif
#ifndef TFT_CS
#define TFT_CS          15
#endif
#ifndef TFT_MISO
#define TFT_MISO        12
#endif
#ifndef TFT_MOSI
#define TFT_MOSI        13
#endif
#ifndef TFT_SCLK
#define TFT_SCLK        14
#endif
#ifndef TFT_BL
#define TFT_BL          21
#endif
#ifndef TFT_BL_LEVEL
#define TFT_BL_LEVEL    1
#endif
#ifndef TFT_RST
#define TFT_RST         -1
#endif
// NUNCHUCK_INPUT (build flag or config_local.h) replaces the controller
#ifdef NUNCHUCK_INPUT
  #ifndef NUNCHUCK_MOVE_THRESHOLD
  #define NUNCHUCK_MOVE_THRESHOLD 30 // stick dead zone
  #endif
#elif !defined(GALAGINO_CONTROLLER)
#define GALAGINO_CONTROLLER
#endif
#ifndef GALAGINO_CONTROLLER_ADDR
#define GALAGINO_CONTROLLER_ADDR 0x09
#endif
#ifndef BTN_START_PIN
#define BTN_START_PIN   35
#endif
#ifndef BTN_LEFT_PIN
#define BTN_LEFT_PIN    35
#endif
#ifndef BTN_RIGHT_PIN
#define BTN_RIGHT_PIN   35
#endif
#ifndef BTN_DOWN_PIN
#define BTN_DOWN_PIN    35
#endif
#ifndef BTN_UP_PIN
#define BTN_UP_PIN      35
#endif
#ifndef BTN_FIRE_PIN
#define BTN_FIRE_PIN    35
#endif

#endif // HAL_BOARD_CYD_CONTROLLER_H
