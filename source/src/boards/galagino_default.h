// Original galagino wiring (ESP32 devkit, ILI9341). Used without BOARD_* and
// without CHEAP_YELLOW_DISPLAY_CONF; config_local.h replaces it by defining
// USE_PIO_CONFIG with its own pins.
#pragma once

#ifndef HAL_VIDEO_CHOSEN
#define HAL_VIDEO_SPI_TFT
#endif
#ifndef HAL_AUDIO_CHOSEN
#define HAL_AUDIO_ESP_DAC
#endif
#ifndef TFT_CS
#define TFT_CS          5
#endif
#ifndef TFT_DC
#define TFT_DC          4
#endif
#ifndef TFT_RST
#define TFT_RST         22
#endif
#ifndef TFT_BL
#define TFT_BL          15      // don't set if backlight is hard wired
#endif
#ifndef TFT_BL_LEVEL
#define TFT_BL_LEVEL    0       // backlight on with low or high signal
#endif
#ifndef TFT_ILI9341
#define TFT_ILI9341             // define for ili9341, otherwise st7789
#endif
//#define TFT_VFLIP               // define for upside down

#ifndef TFT_MISO
#define TFT_MISO        19
#endif
#ifndef TFT_MOSI
#define TFT_MOSI        23
#endif
#ifndef TFT_SCLK
#define TFT_SCLK        18
#endif

// Pins used for buttons
#ifndef BTN_START_PIN
#define BTN_START_PIN   0       // BOOT button
#endif
//#define BTN_COIN_PIN    21      // if this is not defined, then start will act as coin & start

#ifndef NUNCHUCK_INPUT
  #ifndef BTN_LEFT_PIN
  #define BTN_LEFT_PIN  33
  #endif
  #ifndef BTN_RIGHT_PIN
  #define BTN_RIGHT_PIN 14
  #endif
  #ifndef BTN_DOWN_PIN
  #define BTN_DOWN_PIN  16
  #endif
  #ifndef BTN_UP_PIN
  #define BTN_UP_PIN    21
  #endif
  #ifndef BTN_FIRE_PIN
  #define BTN_FIRE_PIN  12
  #endif
#else
  #ifndef NUNCHUCK_SDA
  #define NUNCHUCK_SDA  33
  #endif
  #ifndef NUNCHUCK_SCL
  #define NUNCHUCK_SCL  32
  #endif
  #ifndef NUNCHUCK_MOVE_THRESHOLD
  #define NUNCHUCK_MOVE_THRESHOLD 30 // This is the dead-zone for where minor movements on the stick will not be considered valid movements
  #endif
#endif
