#ifndef _CONFIG_H_
#define _CONFIG_H_

// Optional, git-ignored per-board settings. Defines there or in
// platformio.ini build_flags win over the defaults below. Define
// USE_PIO_CONFIG there to supply your own pins.
// config_local.h may still say HIGH/LOW (Arduino names, same values and
// spelling as Arduino.h): the build has no Arduino.h.
#ifndef HIGH
#define HIGH 0x1
#endif
#ifndef LOW
#define LOW  0x0
#endif
// Board profile (src/boards/): the build selects one with BOARD_<NAME>. A
// profile sets pins, video and controller; config_local.h then keeps its own
// pin block out (USE_PIO_CONFIG). Without a profile: boards/cyd_manual.h if
// CHEAP_YELLOW_DISPLAY_CONF is defined, else boards/galagino_default.h.
#if defined(BOARD_PICO2)
  #define HAL_BOARD_PROFILE "boards/pico2.h"
#elif defined(BOARD_FEATHER_RP2350)
  #define HAL_BOARD_PROFILE "boards/feather_rp2350.h"
#elif defined(BOARD_PICO2W_DVI)
  #define HAL_BOARD_PROFILE "boards/pico2w_dvi.h"
#elif defined(BOARD_CYD2432S028R)
  #define HAL_BOARD_PROFILE "boards/cyd2432s028r.h"
#elif defined(BOARD_FNK0103F)
  #define HAL_BOARD_PROFILE "boards/fnk0103f.h"
#elif defined(BOARD_FNK0103B)
  #define HAL_BOARD_PROFILE "boards/fnk0103b.h"
#elif defined(BOARD_FNK0104)
  #define HAL_BOARD_PROFILE "boards/fnk0104.h"
#endif
#ifdef HAL_BOARD_PROFILE
  #define USE_PIO_CONFIG
#endif

#if __has_include("config_local.h") && !defined(HAL_RP2350)
#include "config_local.h"
#endif

// Backend axes already chosen by a build flag or config_local.h: the profile
// keeps its hands off them.
#if defined(HAL_VIDEO_SPI_TFT) || defined(HAL_VIDEO_NULL) || defined(HAL_VIDEO_DVI_HSTX)
  #define HAL_VIDEO_CHOSEN
#endif
#if defined(HAL_AUDIO_ESP_DAC) || defined(HAL_AUDIO_ES8311) || defined(HAL_AUDIO_NULL) || \
    defined(HAL_AUDIO_DVI)
  #define HAL_AUDIO_CHOSEN
#endif

#if defined(HAL_BOARD_PROFILE)
  #include HAL_BOARD_PROFILE
#elif !defined(USE_PIO_CONFIG)
  #ifdef CHEAP_YELLOW_DISPLAY_CONF
    #include "boards/cyd_manual.h"
  #else
    #include "boards/galagino_default.h"
  #endif
#endif

// renamed: TFT_SPI_PORT counts SPI peripherals (2 = SPI2, 3 = SPI3)
#ifdef TFT_SPI_HOST
  #error "TFT_SPI_HOST is now TFT_SPI_PORT: 2 for SPI2_HOST, 3 for SPI3_HOST"
#endif

// board mounted USB connector up: the panel is turned by 180 degrees
#ifdef BOARD_USBUP
  #ifdef TFT_VFLIP
    #undef TFT_VFLIP
  #else
    #define TFT_VFLIP
  #endif
#endif

// start volume: output = sample / AUDIO_VOLUME, 1 (loudest) .. 30;
// EXTRA + up/down changes it at run time
#ifndef AUDIO_VOLUME
#define AUDIO_VOLUME 3
#endif

// game config
#ifndef MASTER_ATTRACT_MENU_TIMEOUT
#define MASTER_ATTRACT_MENU_TIMEOUT  20000      // start games while sitting idle in menu for 20 seconds, undefine to disable
#endif
#ifndef MASTER_ATTRACT_GAME_TIMEOUT
#define MASTER_ATTRACT_GAME_TIMEOUT  60000 * 5  // restart after 5 minutes
#endif

// ASSETS_REMOTE: menu attract starts only downloaded games, 0 = downloads too
#ifndef NO_DOWNLOADS_IN_ATTRACT
#define NO_DOWNLOADS_IN_ATTRACT 1
#endif

#define MASTER_ATTRACT_MENU_SHOW_COUNTDOWN      // undefine to hide the countdown bar in the menu
#ifndef MASTER_ATTRACT_MENU_COUNTDOWN_BAR_COLOR565
#define MASTER_ATTRACT_MENU_COUNTDOWN_BAR_COLOR565 0x1234  // byte-swapped RGB565 0x3412, #318294 teal blue
#endif

//#define MENU_CYLINDER   // scrolling menu: logos on a rotating drum instead of a flat list

//#define COMMANDO_INVINCIBLE // cheat: Commando player never dies
//#define COMMANDO_NO_MUSIC   // test: Commando sound effects only, no SID music

//#define MARBLE_MADNESS_CHEAT_EXTRA_TIME // cheat: Marble Madness adds 99 s per race

//#define STARWARS_INVERT_Y   // Star Wars: invert stick Y axis

// Mario Bros.: fit 256 native columns, define one
#if !defined(MARIO_SCROLL) && !defined(MARIO_SCALE) && !defined(MARIO_CROP)
#define MARIO_SCROLL        // 240 columns, window pans after player 1
//#define MARIO_SCALE       // 240 columns, every 16th column dropped
//#define MARIO_CROP        // 240 columns, 8 columns dropped left and right
#endif

// Bubble Bobble: fit 256 native columns, define one
#if !defined(BUBLBOBL_SCALE) && !defined(BUBLBOBL_CROP) && !defined(BUBLBOBL_SCALE_BORDER)
#define BUBLBOBL_SCALE      // 224 columns, every 8th column dropped
//#define BUBLBOBL_CROP     // 240 columns, 8 columns dropped left and right
//#define BUBLBOBL_SCALE_BORDER // 240 columns, every 2nd column of the 16 px side walls dropped
#endif

//#define HERO_SCALE_Y // H.E.R.O.: scale 192 lines to 288 (fills the screen, distorts the aspect)
//#define HERO_NTSC_COLORS // H.E.R.O.: NTSC cart colours, default PAL cart colours (Stella palette)

// video config
//#define TFT_SPICLK  40000000    // 40 Mhz. Some displays cope with 80 Mhz
//#define TFT_SPICLK  80000000    // 80 Mhz. Some displays cope with 80 Mhz

// ILI9341 don't work reliably above 60 Mhz, ST7789 works fine at 80 Mhz

#ifndef TFT_SPICLK
#define TFT_SPICLK  40000000 // safe default in not defined on platformio.ini
#endif

// max possible video rate:
// 8*224 pixels = 8*224*16 = 28672 bits
// 2790 char rows per sec at 40Mhz = max 38 fps
#if TFT_SPICLK < 80000000
  #define VIDEO_HALF_RATE
#endif

// x and y offset of 224x288 pixels inside the 240x320 screen
#define TFT_X_OFFSET      8
#define TFT_Y_OFFSET      16

// led config
//#define LED_PIN           18 // pin used for optional WS2812 stripe
#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS 	  50 // range 0..255
#endif

// audio config
//#define SND_DIFF   	 // set to output differential audio on GPIO25 _and_ inverted on GPIO26
#ifndef SND_RIGHT_CHANNEL // Use GPIO 25 for audio
#define SND_LEFT_CHANNEL // Use GPIO 26 for audio
#endif

#ifndef TFT_SPI_PORT

#if CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3
// SPI0/SPI1 -> FLASH/PSRAM
// SPI2      -> 80MHz IO_MUX pins 9/10/11/12/13/14
// SPI3      -> 40Mhz GPIO matrix any pins
#define TFT_SPI_PORT 2  // SPI peripheral number: 2 = SPI2, 3 = SPI3
#else
// ESP32
// SPI0/SPI1 -> FLASH/PSRAM
// SPI2      -> 80MHz 2/4/12=HSPI_MISO/13=HSPI_MOSI/14=HSPI_CLK/15=HSPI_CS0
// SPI3      -> 80Mhz 5=VSPI_CS0/18=VSPI_CLK/19=VSPI_MISO/23=VSPI_MOSI 22=VSPI_WP 21=VSPI_HD
#define TFT_SPI_PORT 2  // SPI peripheral number: 2 = SPI2, 3 = SPI3
#endif

#endif

// Backends neither a build flag, config_local.h nor the profile chose:
// the platform default (ESP: panel on SPI and built-in DAC, else null).
#if !defined(HAL_VIDEO_SPI_TFT) && !defined(HAL_VIDEO_NULL) && !defined(HAL_VIDEO_DVI_HSTX)
  #if defined(HAL_ESP)
    #define HAL_VIDEO_SPI_TFT
  #else
    #define HAL_VIDEO_NULL
  #endif
#endif
#if !defined(HAL_AUDIO_ESP_DAC) && !defined(HAL_AUDIO_ES8311) && !defined(HAL_AUDIO_NULL) && \
    !defined(HAL_AUDIO_DVI)
  #if defined(HAL_ESP)
    #define HAL_AUDIO_ESP_DAC
  #else
    #define HAL_AUDIO_NULL
  #endif
#endif

#include "hal/config_check.h"

#endif // _CONFIG_H_
