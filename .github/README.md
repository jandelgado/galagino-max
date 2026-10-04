GalaginoMAX
===========

This is my (JD) custom Galagino build with Zaxxon, Centipede, Millipede,
Asteroids, Baluba and working Road Fighter added. It uses compression and
allows to squeeze all games at once into the 4MB flash of the classic ESP32
module.

It is based on [galagino/galagino](https://github.com/galagino/galagino](galagino/galagino) and
it has Moon Cresta, Scramble and Super Cobra and the games from [speckhoiler/galagino](https://github.com/speckhoiler/galagino), from [SurvivalHacking/galagino3](https://github.com/SurvivalHacking/galagino3), [SurvivalHacking/spinnerino](https://github.com/SurvivalHacking/spinnerino), [VirtualClaudioBoy/GalaginoPlus](https://github.com/VirtualClaudioBoy/GalaginoPlus) and [BaasPierre/GalaginoPlusGoldstar](https://github.com/BaasPierre/GalaginoPlusGoldstar). Original Galagino by Till Harbaum [harbaum/galagino](https://github.com/harbaum/galagino)


### Quality of Life, improvements and fixes

* `TFT_VFLIP` logic reworked to keep track of state and work across ST7789 and ILI9341
* `TFT_INVERT` added to support CYD clones that show inverted colors
* `m6809` emulation uses machineBase methods, so you can have multiple instances just like the `Z80` and `i8048`
* Time Pilot sprite multiplexing
* Bluetooth Controller over i2c see: [galagino-controller](https://github.com/galagino/galagino-controller)
* Bluetooth Controller supports multiple action/fire/bomb buttons.
* Support for ESP32-S3 CYD clone with 16MiB Flash
* Support for External DAC (es8311). ESP32-S3's don't have internal DACs.
* Linux `romconv` scripts.
* `pengo.zip`, `pengoj.zip` romsets conversion (which is the one with the popcorn music).
* Enabled machines selection moved to `platformio.ini`
* File generation without unziping, some roms cause name clashes and is much cleaner. Not all roms yet.
* Flash and PSRAM SPI configs for maximum speed available on each ESP32 version.
* Many code cleanups for reduced RAM used - with 44 games around 260k free heap ~~(Flash is the limiting factor, you need and ESP32 with 8MiB of flash)~~.
* mos6502 emulation
* Flash compression allows to include ALL games even in the ESP32 4MB flash versions
* Optional on-screen FPS display (`DEBUG_TIMING_FPS_HUD`)
* Optional cylinder menu, logos on a rotating drum (`MENU_CYLINDER`)
* A nice arcade style startup self-test (`BOOT_SELFTEST`)

### Hardware Used

| Board    | Link                                                            | Amazon                                  | Notes               |
| ---      | ---                                                             | ---                                     | ---                 |
| fnk0103b | [github](https://github.com/Freenove/Freenove_ESP32_Display)    | [Amzn](https://amazon.es/dp/)           | ST7789 - SPI 80MHz  |
|          |                                                                 |                                         |                     |
| fnk0103f | [github](https://github.com/Freenove/Freenove_ESP32_Display)    | [Amzn](https://amazon.es/dp/)           | ILI9341 - SPI 40MHz |
|          |                                                                 |                                         |                     |
| fnk0104a | [github](https://github.com/Freenove/Freenove_ESP32_S3_Display) | [Amzn](https://amazon.es/dp/B0FSQLPQ6M) | ESP32-S3 - IPS Display - External DAC |
|          |                                                                 |                                         | ILI9341 - SPI 40MHz - 16MiB Flash     |

### 3d printed enclosure

I've used [Gavin Knight's](https://www.hackster.io/dynamight/cyd-galagino-arcade-cabinet-369ce9) very nice enclosure.

### Building

Place the ROM zips in [romszip](/romszip/), then run `romconv/convert.sh`
(Linux/macOS) or `romconv/convert.bat` (Windows). 

### Configuration

Keep personal settings out of `config.h` so it stays close to upstream.
Put them in `source/src/config_local.h` instead. The file is git-ignored
and included at the top of `config.h` if present. Precedence:
`platformio.ini` build flags, then `config_local.h`, then the `config.h`
defaults. Example:

```c
#define MASTER_ATTRACT_GAME_TIMEOUT  60000 * 2
#define LED_PIN           16
#define SND_RIGHT_CHANNEL         // audio on GPIO 25 instead of 26

// own pins: skip the board block in config.h
#define USE_PIO_CONFIG
#define TFT_CS            5
#define TFT_DC            32
// ... remaining TFT_* and BTN_* pins
```

### Attract mode

Settings in `config.h`:

* `MASTER_ATTRACT_MENU_TIMEOUT`: idle time in the menu before a game starts (ms).
* `MASTER_ATTRACT_GAME_TIMEOUT`: time until an attract game ends (ms).
* `MASTER_ATTRACT_MENU_SHOW_COUNTDOWN`: shows a bar in the menu with the time
  left until a game starts. Comment out in `config.h` to hide it.
* `MASTER_ATTRACT_MENU_COUNTDOWN_BAR_COLOR565`: bar color, byte-swapped RGB565
  (SPI byte order).

### FPS display

Add `-D DEBUG_TIMING_FPS_HUD` to `build_flags` in `platformio.ini` to show
the emulation frame rate at the bottom of the screen. Off by default.

### Cylinder menu

Shows the menu logos on a rotating drum instead of a flat list. Off by
default. Enable with `#define MENU_CYLINDER` in `config_local.h`, or add
`-D MENU_CYLINDER` to `build_flags` in `platformio.ini`.

### Boot self-test

Arcade style boot screen: with RAM, ROM etc. infos and LED test. Takes about
5s.  Enable with `#define BOOT_SELFTEST` in `config_local.h`, or add `-D
BOOT_SELFTEST` to `build_flags` in `platformio.ini`. 

### Games

| Game                           | Marquee                       | Screenshot                     | Notes |
| ---                            | ---                           | ---                            | ---   |
| Pac-Man (pacman.zip)           | ![ ](/logos/pacman.png)       | ![ ](/images/pacman.gif)       |       |
| Galaga (galaga.zip)            | ![ ](/logos/galaga.png)       | ![ ](/images/galagino.gif)     |       |
| Dig Dug (digdug.zip)           | ![ ](/logos/digdug.png)       | ![ ](/images/digdug.png)       |       |
| Frogger (frogger.zip)          | ![ ](/logos/frogger.png)      | ![ ](/images/frogger.png)      |       |
| Donkey Kong (dkong.zip)        | ![ ](/logos/dkong.png)        | ![ ](/images/dkong.gif)        |       |
| 1942 (1942.zip)                | ![ ](/logos/1942.png)         | ![ ](/images/1942.png)         | L1-Fire R1-Loop |
| Lizard Wizard (lizwiz.zip)     | ![ ](/logos/lizwiz.png)       | ![ ](/images/lizwiz.png)       |       |
| Eyes (eyes.zip)                | ![ ](/logos/eyes.png)         | ![ ](/images/eyes.png)         |       |
| Mr. TNT (mrtnt.zip)            | ![ ](/logos/mrtnt.png)        | ![ ](/images/mrtnt.png)        |       |
| The Glob (theglobp.zip)        | ![ ](/logos/theglob.png)      | ![ ](/images/theglob.png)      |       |
| Crush Roller (crush.zip)       | ![ ](/logos/crush.png)        | ![ ](/images/crush.png)        |       |
| Ant Eater (anteater.zip)       | ![ ](/logos/anteater.png)     | ![ ](/images/anteater.png)     |       |
| Bombjack (bombjack.zip)        | ![ ](/logos/bombjack.png)     | ![ ](/images/bombjack.png)     |       |
| Mr. Do! (mrdo.zip)             | ![ ](/logos/mrdo.png)         | ![ ](/images/mrdo.png)         |       |
| Bagman (bagmanm2.zip)          | ![ ](/logos/bagman.png)       | ![ ](/images/bagman.png)       |       |
| Pengo (pengo2u.zip)            | ![ ](/logos/pengo.png)        | ![ ](/images/pengo.png)        |       |
| MsPacman (mspacman.zip)        | ![ ](/logos/mspacman.png)     | ![ ](/images/mspacman.png)     |       |
| Galaxian (galaxian.zip)        | ![ ](/logos/galaxian.png)     | ![ ](/images/galaxian.png)     |       |
| LadyBug (ladybug.zip)          | ![ ](/logos/ladybug.png)      | ![ ](/images/ladybug.png)      |       |
| Space Invaders (invaders.zip)  | ![ ](/logos/invaders.png)     | ![ ](/images/invaders.png)     |       |
| Time Pilot (timeplt.zip)       | ![ ](/logos/timeplt.png)      | ![ ](/images/timeplt.png)      |       |
| Gyruss (gyruss.zip)            | ![ ](/logos/gyruss.png)       | ![ ](/images/gyruss.png)       |       |
| Tutankham (tutankhm.zip)       | ![ ](/logos/tutankhm.png)     | ![ ](/images/tutankham.png)    | ABXY=Fire START+COIN+L1+R1=Bomb |
| Donkey Kong Jr. (dkongjrj.zip) | ![ ](/logos/dkongjr.png)      | ![ ](/images/dkongjr.png)      |       |
| Star Force (starforc.zip)      | ![ ](/logos/starforce.png)    | ![ ](/images/starforce.png)    |       |
| Moon Cresta (mooncrst.zip)     | ![ ](/logos/mooncresta.png)   | ![ ](/images/mooncresta.png)   |       |
| Scramble (scramble.zip)        | ![_](/logos/scramble.png)     | ![_](/images/scramble.png)     | A+X=Fire B+Y+R1=Bomb |
| Super Cobra (scobra.zip)       | ![_](/logos/supercobra.png)   | ![_](/images/supercobra.png)   |       |
| Donkey Kong 3 (dkong3.zip)     | ![_](/logos/dkong3.png)       | ![_](/images/dkong3.png)       |       |
| Pooyan (pooyan.zip)            | ![_](/logos/pooyan.png)       | ![_](/images/pooyan.png)       |       |
| Phoenix (phoenix.zip)          | ![_](/logos/phoenix.png)      | ![_](/images/phoenix.png)      |       |
| Burger Time (btime.zip)        | ![_](/logos/burgertime.png)   | ![_](/images/burgertime.png)   |       |
| Xevious (xevious.zip)          | ![_](/logos/xevious.png)      | ![_](/images/xevious.png)      |       |
| Bump 'n' Jump (bnj.zip)        | ![_](/logos/bnj.png)          | ![_](/images/bnj.png)          |       |
| Mappy (mappy.zip)              | ![_](/logos/mappy.png)        | ![_](/images/mappy.png)        |       |
| Gaplus (gaplus.zip)            | ![_](/logos/gaplus.png)       | ![_](/images/gaplus.png)       |       |
| Alibaba (alibaba.zip)          | ![_](/logos/alibaba.png)      | ![_](/images/alibaba.png)      |       |
| Amidar (amidar.zip)            | ![_](/logos/amidar.png)       | ![_](/images/amidar.png)       |       |
| Turtles (turtles.zip)          | ![_](/logos/turtles.png)      | ![_](/images/turtles.png)      |       |
| Circus Charlie (circusc.zip)   | ![_](/logos/circusc.png)      | ![_](/images/circusc.png)      |       |
| Roc'n Rope (rocnrope.zip)      | ![_](/logos/rocnrope.png)     | ![_](/images/rocnrope.png)     |       |
| Tower of Druaga (todruaga.zip) | ![_](/logos/todruaga.png)     | ![_](/images/todruaga.png)     |       |
| Van Van Car (vanvan.zip)       | ![_](/logos/vanvan.png)       | ![_](/images/vanvan.png)       |       |
| Pinball Action (pbaction.zip)  | ![_](/logos/pbaction.png)     | ![_](/images/pbaction.png)     |       |
| Fantasy Island (fantasyu.zip)  | ![_](/logos/fantasy.png)      | ![_](/images/fantasy.png)      |       |
| Nibbler (nibblerp.zip)         | ![_](/logos/nibbler.png)      | ![_](/images/nibbler.png)      |       |
| Vanguard (vanguard.zip)        | ![_](/logos/vanguard.png)     | ![_](/images/vanguard.png)     |       |
| Scrambled Egg (scregg.zip)     | ![_](/logos/scregg.png)       | ![_](/images/scregg.png)       | Broken             |
| Road Fighter (roadf2.zip)      | ![_](/logos/roadfighter.png)  | ![_](/images/roadfighter.png)  |  |                
| Motorace USA (motorace.zip)    | ![_](/logos/motorace.png)     | ![_](/images/motorace.png)     | x.y = 256x240      |
| Zaxxon (zaxxon.zip)            | ![_](/logos/zaxxon.png)       |                                | US Rev D. Audio samples: zaxxon-audio.zip |
| Centipede (centiped.zip)            | ![_](/logos/centipede.png)       |                                |  |
| Millipede (milliped.zip)            | ![_](/logos/milliped.png)        |                                |  |
| Asteroids (asteroid.zip)       | ![_](/logos/asteroids.png)    |                                | Vector display rasterized |
| Baluba-louk no Densetsu (baluba.zip) | ![_](/logos/baluba.png) |                          | Star Force board |

### ...
