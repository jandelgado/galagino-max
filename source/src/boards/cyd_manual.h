// Cheap Yellow Display wired by hand: GPIO buttons or a Nunchuk on CN1.
// Selected by CHEAP_YELLOW_DISPLAY_CONF (config_local.h or a build flag).
#ifndef HAL_BOARD_CYD_MANUAL_H
#define HAL_BOARD_CYD_MANUAL_H

#ifndef HAL_VIDEO_CHOSEN
#define HAL_VIDEO_SPI_TFT
#endif
#ifndef HAL_AUDIO_CHOSEN
#define HAL_AUDIO_ESP_DAC
#endif
#ifndef TFT_CS
#define TFT_CS          15
#endif
#ifndef TFT_DC
#define TFT_DC          2
#endif
#ifndef TFT_RST
#define TFT_RST         -1
#endif
#ifndef TFT_BL
#define TFT_BL          21   // don't set if backlight is hard wired
#endif
#ifndef TFT_BL_LEVEL
#define TFT_BL_LEVEL    1     // backlight on with low or high signal
#endif
//#define TFT_ILI9341 // define for ili9341, otherwise st7789
//#define TFT_VFLIP   // define for upside down

#ifndef TFT_MISO
#define TFT_MISO        12
#endif
#ifndef TFT_MOSI
#define TFT_MOSI        13
#endif
#ifndef TFT_SCLK
#define TFT_SCLK        14
#endif
//#define TFT_MAC       0x20  // some CYD need this to rotate properly and have correct colors

// Pins used for buttons
#ifndef BTN_START_PIN
#define BTN_START_PIN   35
#endif
//#define BTN_COIN_PIN    21   // if this is not defined, then start will act as coin & start

#ifndef NUNCHUCK_INPUT
  #ifndef BTN_LEFT_PIN
  #define BTN_LEFT_PIN    23
  #endif
  #ifndef BTN_RIGHT_PIN
  #define BTN_RIGHT_PIN   22
  #endif
  #ifndef BTN_DOWN_PIN
  #define BTN_DOWN_PIN    16
  #endif
  #ifndef BTN_UP_PIN
  #define BTN_UP_PIN      17
  #endif
  #ifndef BTN_FIRE_PIN
  #define BTN_FIRE_PIN    4
  #endif
#else
  // CN1 connector
  #ifndef NUNCHUCK_SDA
  #define NUNCHUCK_SDA  22
  #endif
  #ifndef NUNCHUCK_SCL
  #define NUNCHUCK_SCL  27
  #endif
  #ifndef NUNCHUCK_MOVE_THRESHOLD
  #define NUNCHUCK_MOVE_THRESHOLD 30 // This is the dead-zone for where minor movements on the stick will not be considered valid movements
  #endif
#endif

#endif // HAL_BOARD_CYD_MANUAL_H
