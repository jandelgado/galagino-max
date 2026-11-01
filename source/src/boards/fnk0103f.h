// Freenove FNK0103F (transparent), ILI9341, SPI clock 40 MHz.
// https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial/Freenove_ESP32_Display.html
#ifndef HAL_BOARD_FNK0103F_H
#define HAL_BOARD_FNK0103F_H

#include "cyd_controller.h"
#ifndef AUDIO_ENABLE_PIN
#define AUDIO_ENABLE_PIN 4
#endif
#ifndef TFT_ILI9341
#define TFT_ILI9341
#endif
#ifndef TFT_SPI_PORT
#define TFT_SPI_PORT    2
#endif
#ifndef TFT_SPICLK
#define TFT_SPICLK      40000000L
#endif
#ifndef GALAGINO_CONTROLLER_SDA
#define GALAGINO_CONTROLLER_SDA 18
#endif
#ifndef GALAGINO_CONTROLLER_SCL
#define GALAGINO_CONTROLLER_SCL 19
#endif
#ifndef MENU_CYLINDER
#define MENU_CYLINDER
#endif

#endif // HAL_BOARD_FNK0103F_H
