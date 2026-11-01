// ESP32-2432S028R (Cheap Yellow Display), ILI9341, SPI clock 40 MHz.
#ifndef HAL_BOARD_CYD2432S028R_H
#define HAL_BOARD_CYD2432S028R_H

#include "cyd_controller.h"
#ifndef TFT_ILI9341
#define TFT_ILI9341
#endif
#ifndef TFT_SPICLK
#define TFT_SPICLK      40000000L
#endif
#ifndef GALAGINO_CONTROLLER_SDA
#define GALAGINO_CONTROLLER_SDA 27
#endif
#ifndef GALAGINO_CONTROLLER_SCL
#define GALAGINO_CONTROLLER_SCL 22
#endif
// Nunchuk wiring of the pre-board-profile CYD config
#ifndef NUNCHUCK_SDA
#define NUNCHUCK_SDA 22
#endif
#ifndef NUNCHUCK_SCL
#define NUNCHUCK_SCL 27
#endif

#endif // HAL_BOARD_CYD2432S028R_H
