// Freenove FNK0103B (black), ST7789, SPI clock 80 MHz, panel mounted upside down.
#ifndef HAL_BOARD_FNK0103B_H
#define HAL_BOARD_FNK0103B_H

#include "cyd_controller.h"
#ifndef AUDIO_ENABLE_PIN
#define AUDIO_ENABLE_PIN 4
#endif
#ifndef TFT_ST7789
#define TFT_ST7789
#endif
#ifndef TFT_SPI_PORT
#define TFT_SPI_PORT    2
#endif
#ifndef TFT_SPICLK
#define TFT_SPICLK      80000000L
#endif
#ifndef GALAGINO_CONTROLLER_SDA
#define GALAGINO_CONTROLLER_SDA 18
#endif
#ifndef GALAGINO_CONTROLLER_SCL
#define GALAGINO_CONTROLLER_SCL 19
#endif
#ifndef TFT_VFLIP
#define TFT_VFLIP
#endif

#endif // HAL_BOARD_FNK0103B_H
