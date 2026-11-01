// Freenove FNK0104 (ESP32-S3, silk white), ILI9341, SPI clock 40 MHz, ES8311 codec.
#ifndef HAL_BOARD_FNK0104_H
#define HAL_BOARD_FNK0104_H

#ifndef HAL_VIDEO_CHOSEN
#define HAL_VIDEO_SPI_TFT
#endif
#ifndef AUDIO_ENABLE_PIN
#define AUDIO_ENABLE_PIN 1
#endif
#ifndef TFT_ILI9341
#define TFT_ILI9341
#endif
#ifndef TFT_INVERT
#define TFT_INVERT
#endif
#ifndef TFT_SPICLK
#define TFT_SPICLK      40000000L
#endif
#ifndef TFT_CS
#define TFT_CS          10
#endif
#ifndef TFT_DC
#define TFT_DC          46
#endif
#ifndef TFT_MISO
#define TFT_MISO        13
#endif
#ifndef TFT_MOSI
#define TFT_MOSI        11
#endif
#ifndef TFT_SCLK
#define TFT_SCLK        12
#endif
#ifndef TFT_SPI_PORT
#define TFT_SPI_PORT    2
#endif
#ifndef TFT_BL
#define TFT_BL          45
#endif
#ifndef TFT_BL_LEVEL
#define TFT_BL_LEVEL    1
#endif
#ifndef TFT_RST
#define TFT_RST         -1
#endif
// NUNCHUCK_INPUT (build flag or config_local.h) replaces the controller, on
// the same I2C connector (shared with the codec)
#ifdef NUNCHUCK_INPUT
  #ifndef NUNCHUCK_SDA
  #define NUNCHUCK_SDA 16
  #endif
  #ifndef NUNCHUCK_SCL
  #define NUNCHUCK_SCL 15
  #endif
  #ifndef NUNCHUCK_MOVE_THRESHOLD
  #define NUNCHUCK_MOVE_THRESHOLD 30 // stick dead zone
  #endif
#elif !defined(GALAGINO_CONTROLLER)
#define GALAGINO_CONTROLLER
#endif
#ifndef GALAGINO_CONTROLLER_ADDR
#define GALAGINO_CONTROLLER_ADDR 0x09
#endif
#ifndef GALAGINO_CONTROLLER_SDA
#define GALAGINO_CONTROLLER_SDA 16
#endif
#ifndef GALAGINO_CONTROLLER_SCL
#define GALAGINO_CONTROLLER_SCL 15
#endif
#ifndef HAL_AUDIO_CHOSEN
#define HAL_AUDIO_ES8311
#endif
#ifndef ES8311_I2C_CLK
#define ES8311_I2C_CLK  400000
#endif
#ifndef ES8311_I2C_ADDR
#define ES8311_I2C_ADDR 0x18
#endif
#ifndef ES8311_I2C_SDA
#define ES8311_I2C_SDA  16
#endif
#ifndef ES8311_I2C_SCL
#define ES8311_I2C_SCL  15
#endif
#ifndef ES8311_I2S_MCK
#define ES8311_I2S_MCK  4
#endif
#ifndef ES8311_I2S_BCK
#define ES8311_I2S_BCK  5
#endif
#ifndef ES8311_I2S_DIN
#define ES8311_I2S_DIN  6
#endif
#ifndef ES8311_I2S_DOUT
#define ES8311_I2S_DOUT 8
#endif
#ifndef ES8311_I2S_WS
#define ES8311_I2S_WS   7
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

#endif // HAL_BOARD_FNK0104_H
