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

### Load ROMs over WiFi

Puts all games on an ESP32 with 4 MB flash and no PSRAM. A set of games is
built into the firmware. Every other game downloads its ROM data over HTTP
when started and stays cached in a 1 MB flash partition, oldest download
evicted first. Menu entries not cached yet show a NET marker.

1. Configure in `config_local.h`:

   ```c
   #define GALAGINO_WIFI_SSID   "my-ssid"
   #define GALAGINO_WIFI_PASS   "my-password"
   #define GALAGINO_ASSETS_URL  "http://192.168.1.2:8080"  // IP, no DNS
   ```

2. Build the bundles: `romconv/convert.sh` also writes one bundle per
   downloadable game to `galagino-server/data/g/<hash>`. The firmware
   embeds the hashes, so rebuild it after each conversion.

3. Serve the bundles, e.g. with Python:

   ```sh
   python3 -m http.server 8080 --directory galagino-server/data
   ```

4. Flash a `<board>-remote` env, e.g. `make azdelivery-remote` in
   `source/`. `[mode_remote]` in `platformio.ini` lists the built-in games.

### Games

| Game                           | Marquee                       | Screenshot                     | Notes |
| ---                            | ---                           | ---                            | ---   |
| Pac-Man (pacman.zip)           | ![Pac-Man marquee](/logos/pacman.png)       | ![Pac-Man screenshot](/images/pacman.gif)       |       |
| Galaga (galaga.zip)            | ![Galaga marquee](/logos/galaga.png)       | ![Galaga screenshot](/images/galagino.gif)     |       |
| Dig Dug (digdug.zip)           | ![Dig Dug marquee](/logos/digdug.png)       | ![Dig Dug screenshot](/images/digdug.png)       |       |
| Frogger (frogger.zip)          | ![Frogger marquee](/logos/frogger.png)      | ![Frogger screenshot](/images/frogger.png)      |       |
| Donkey Kong (dkong.zip)        | ![Donkey Kong marquee](/logos/dkong.png)        | ![Donkey Kong screenshot](/images/dkong.gif)        |       |
| 1942 (1942.zip)                | ![1942 marquee](/logos/1942.png)         | ![1942 screenshot](/images/1942.png)         | L1-Fire R1-Loop |
| Lizard Wizard (lizwiz.zip)     | ![Lizard Wizard marquee](/logos/lizwiz.png)       | ![Lizard Wizard screenshot](/images/lizwiz.png)       |       |
| Eyes (eyes.zip)                | ![Eyes marquee](/logos/eyes.png)         | ![Eyes screenshot](/images/eyes.png)         |       |
| Mr. TNT (mrtnt.zip)            | ![Mr. TNT marquee](/logos/mrtnt.png)        | ![Mr. TNT screenshot](/images/mrtnt.png)        |       |
| The Glob (theglobp.zip)        | ![The Glob marquee](/logos/theglob.png)      | ![The Glob screenshot](/images/theglob.png)      |       |
| Crush Roller (crush.zip)       | ![Crush Roller marquee](/logos/crush.png)        | ![Crush Roller screenshot](/images/crush.png)        |       |
| Ant Eater (anteater.zip)       | ![Ant Eater marquee](/logos/anteater.png)     | ![Ant Eater screenshot](/images/anteater.png)     |       |
| Bombjack (bombjack.zip)        | ![Bombjack marquee](/logos/bombjack.png)     | ![Bombjack screenshot](/images/bombjack.png)     |       |
| Mr. Do! (mrdo.zip)             | ![Mr. Do! marquee](/logos/mrdo.png)         | ![Mr. Do! screenshot](/images/mrdo.png)         |       |
| Bagman (bagmanm2.zip)          | ![Bagman marquee](/logos/bagman.png)       | ![Bagman screenshot](/images/bagman.png)       |       |
| Pengo (pengo.zip)              | ![Pengo marquee](/logos/pengo.png)        | ![Pengo screenshot](/images/pengo.png)        |       |
| MsPacman (mspacman.zip)        | ![MsPacman marquee](/logos/mspacman.png)     | ![MsPacman screenshot](/images/mspacman.png)     |       |
| Galaxian (galaxian.zip)        | ![Galaxian marquee](/logos/galaxian.png)     | ![Galaxian screenshot](/images/galaxian.png)     |       |
| LadyBug (ladybug.zip)          | ![LadyBug marquee](/logos/ladybug.png)      | ![LadyBug screenshot](/images/ladybug.png)      |       |
| Space Invaders (invaders.zip)  | ![Space Invaders marquee](/logos/invaders.png)     | ![Space Invaders screenshot](/images/invaders.png)     |       |
| Time Pilot (timeplt.zip)       | ![Time Pilot marquee](/logos/timeplt.png)      | ![Time Pilot screenshot](/images/timeplt.png)      |       |
| Gyruss (gyruss.zip)            | ![Gyruss marquee](/logos/gyruss.png)       | ![Gyruss screenshot](/images/gyruss.png)       |       |
| Tutankham (tutankhm.zip)       | ![Tutankham marquee](/logos/tutankhm.png)     | ![Tutankham screenshot](/images/tutankham.png)    | ABXY=Fire START+COIN+L1+R1=Bomb |
| Donkey Kong Jr. (dkongjrj.zip) | ![Donkey Kong Jr. marquee](/logos/dkongjr.png)      | ![Donkey Kong Jr. screenshot](/images/dkongjr.png)      |       |
| Star Force (starforc.zip)      | ![Star Force marquee](/logos/starforce.png)    | ![Star Force screenshot](/images/starforce.png)    |       |
| Moon Cresta (mooncrst.zip)     | ![Moon Cresta marquee](/logos/mooncresta.png)   | ![Moon Cresta screenshot](/images/mooncresta.png)   |       |
| Scramble (scramble.zip)        | ![Scramble marquee](/logos/scramble.png)     | ![Scramble screenshot](/images/scramble.png)     | A+X=Fire B+Y+R1=Bomb |
| Super Cobra (scobra.zip)       | ![Super Cobra marquee](/logos/supercobra.png)   | ![Super Cobra screenshot](/images/supercobra.png)   |       |
| Donkey Kong 3 (dkong3.zip)     | ![Donkey Kong 3 marquee](/logos/dkong3.png)       | ![Donkey Kong 3 screenshot](/images/dkong3.png)       |       |
| Pooyan (pooyan.zip)            | ![Pooyan marquee](/logos/pooyan.png)       | ![Pooyan screenshot](/images/pooyan.png)       |       |
| Phoenix (phoenix.zip)          | ![Phoenix marquee](/logos/phoenix.png)      | ![Phoenix screenshot](/images/phoenix.png)      |       |
| Burger Time (btime.zip)        | ![Burger Time marquee](/logos/burgertime.png)   | ![Burger Time screenshot](/images/burgertime.png)   |       |
| Xevious (xevious.zip)          | ![Xevious marquee](/logos/xevious.png)      | ![Xevious screenshot](/images/xevious.png)      |       |
| Bump 'n' Jump (bnj.zip)        | ![Bump 'n' Jump marquee](/logos/bnj.png)          | ![Bump 'n' Jump screenshot](/images/bnj.png)          |       |
| Mappy (mappy.zip)              | ![Mappy marquee](/logos/mappy.png)        | ![Mappy screenshot](/images/mappy.png)        |       |
| Gaplus (gaplus.zip)            | ![Gaplus marquee](/logos/gaplus.png)       | ![Gaplus screenshot](/images/gaplus.png)       |       |
| Alibaba (alibaba.zip)          | ![Alibaba marquee](/logos/alibaba.png)      | ![Alibaba screenshot](/images/alibaba.png)      |       |
| Amidar (amidar.zip)            | ![Amidar marquee](/logos/amidar.png)       | ![Amidar screenshot](/images/amidar.png)       |       |
| Turtles (turtles.zip)          | ![Turtles marquee](/logos/turtles.png)      | ![Turtles screenshot](/images/turtles.png)      |       |
| Circus Charlie (circusc.zip)   | ![Circus Charlie marquee](/logos/circusc.png)      | ![Circus Charlie screenshot](/images/circusc.png)      |       |
| Roc'n Rope (rocnrope.zip)      | ![Roc'n Rope marquee](/logos/rocnrope.png)     | ![Roc'n Rope screenshot](/images/rocnrope.png)     |       |
| Tower of Druaga (todruaga.zip) | ![Tower of Druaga marquee](/logos/todruaga.png)     | ![Tower of Druaga screenshot](/images/todruaga.png)     |       |
| Van Van Car (vanvan.zip)       | ![Van Van Car marquee](/logos/vanvan.png)       | ![Van Van Car screenshot](/images/vanvan.png)       |       |
| Pinball Action (pbaction.zip)  | ![Pinball Action marquee](/logos/pbaction.png)     | ![Pinball Action screenshot](/images/pbaction.png)     |       |
| Fantasy Island (fantasyu.zip)  | ![Fantasy Island marquee](/logos/fantasy.png)      | ![Fantasy Island screenshot](/images/fantasy.png)      |       |
| Nibbler (nibblerp.zip)         | ![Nibbler marquee](/logos/nibbler.png)      | ![Nibbler screenshot](/images/nibbler.png)      |       |
| Vanguard (vanguard.zip)        | ![Vanguard marquee](/logos/vanguard.png)     | ![Vanguard screenshot](/images/vanguard.png)     |       |
| Scrambled Egg (scregg.zip)     | ![Scrambled Egg marquee](/logos/scregg.png)       | ![Scrambled Egg screenshot](/images/scregg.png)       | Broken             |
| Road Fighter (roadf2.zip)      | ![Road Fighter marquee](/logos/roadfighter.png)  | ![Road Fighter screenshot](/images/roadfighter.png)  |  |                
| Motorace USA (motorace.zip)    | ![Motorace USA marquee](/logos/motorace.png)     | ![Motorace USA screenshot](/images/motorace.png)     | x.y = 256x240      |
| Zaxxon (zaxxon.zip)            | ![Zaxxon marquee](/logos/zaxxon.png)       |                                | US Rev D. Audio samples: zaxxon-audio.zip |
| Centipede (centiped.zip)            | ![Centipede marquee](/logos/centipede.png)       |                                |  |
| Millipede (milliped.zip)            | ![Millipede marquee](/logos/milliped.png)        |                                |  |
| Asteroids (asteroid.zip)       | ![Asteroids marquee](/logos/asteroids.png)    |                                | Vector display rasterized |
| Baluba-louk no Densetsu (baluba.zip) | ![Baluba-louk no Densetsu marquee](/logos/baluba.png) |                          | Star Force board |
| Commando (commando.zip)        | ![Commando marquee](/logos/commando.png) | ![Commando screenshot](/images/commando.png) | L1-Fire START+R1=Grenade. C64 SID music: romszip/Commando.sid (HVSC) |
| Lunar Lander (llander.zip)     | ![Lunar Lander marquee](/logos/llander.png) | ![Lunar Lander screenshot](/images/llander.png) | Rev 2. Up/down=thrust lever, FIRE=select mission/abort |
| The Pit (thepit.zip)           | ![The Pit marquee](/logos/thepit.png) | ![The Pit screenshot](/images/thepit.png) |  |
| Bubble Bobble (boblbobl.zip)   | ![Bubble Bobble marquee](/logos/bublbobl.png) | ![Bubble Bobble screenshot](/images/boblbobl.png) | Bootleg "Bobble Bobble", no MCU, needs parent bublbobl.zip. UP=jump. C64 SID music: romszip/Bubble_Bobble.sid (HVSC), effects recorded from MAME (samples/bublbobl_samples.zip) |
| Tetris (atetris.zip)           | ![Tetris marquee](/logos/atetris.png) | ![Tetris screenshot](/images/atetris.png) | Atari, set 1. FIRE=rotate. Screen dynamically scaled and cropped in game for best view: playfield cropped to the player 1 side, other screens scaled from 336 to 240 columns |
| Crystal Castles (ccastlesj.zip) | ![Crystal Castles marquee](/logos/ccastles.png) | ![Crystal Castles screenshot](/images/ccastles.png) | Joystick version, needs parent ccastles.zip. FIRE=jump |
| Battle Zone (bzone.zip)        | ![Battle Zone marquee](/logos/bzone.png) | ![Battle Zone screenshot](/images/bzone.png) | Rev 2. Up/down=both treads, left/right=spin, diagonals=one tread |
| Tempest (tempest.zip)         | ![Tempest marquee](/logos/tempest.png) | ![Tempest screenshot](/images/tempest.png) | Rev 3. Left/right=spinner, FIRE=fire, up/down=superzap |
| Space Duel (spacduel.zip)     | ![Space Duel marquee](/logos/spacduel.png) | ![Space Duel screenshot](/images/spacduel.png) | Version 2, 1 player. Left/right=rotate, UP=thrust, DOWN=shield, FIRE=fire. Up/down selects the game type before start |
| Berzerk (berzerk.zip)         | ![Berzerk marquee](/logos/berzerk.png) | ![Berzerk screenshot](/images/berzerk.png) | Revision RC31A, with working speech output. FIRE+stick=shoot in that direction. 256 columns shown in 240: 4 px black borders cropped, 8 column pairs merged so thin lasers stay visible |
| Gravitar (gravitar.zip)       | ![Gravitar marquee](/logos/gravitar.png) | ![Gravitar screenshot](/images/gravitar.png) | Version 3, 1 player. Left/right=rotate, UP=thrust, DOWN=shield/tractor beam, FIRE=fire |
| Marble Madness (marble.zip)   | ![Marble Madness marquee](/logos/marble.png) | ![Marble Madness screenshot](/images/marble.png) | Native remake: game logic rewritten in C++, graphics and levels from the ROM, needs BIOS atarisy1.zip. No 68010 emulation. The 240 px view scrolls horizontally over the 288 px wide playfield to follow the marble. Stick=trackball, FIRE held accelerates the ball. Effects run the arcade sound program. C64 SID music and jingle: download GAMES/M-R/Marble_Madness.sid from the HVSC (hvsc.c64.org) into romszip/ |
| Star Wars (starwars.zip)       | ![Star Wars marquee](/logos/starwars.png) | ![Star Wars screenshot](/images/starwars.png) | Atari classic with early 3D vector graphics and working speech output. Set 1. Stick moves the yoke, position held on release. FIRE=trigger |
| Missile Command (missile.zip) | ![Missile Command marquee](/logos/missile.png) | ![Missile Command screenshot](/images/missile.png) | Rev 3. Stick moves the cursor. Auto-aim: FIRE launches from the nearest non-empty base to the cross-hair. Controller L1/A/R1=left/center/right base |
| Moon Patrol (mpatrol.zip)     | ![Moon Patrol marquee](/logos/mpatrol.png) | ![Moon Patrol screenshot](/images/mpatrol.png) | UP=jump, FIRE=fire |
| Star Jacker (starjack.zip)    | ![Star Jacker marquee](/logos/starjack.png) | ![Star Jacker screenshot](/images/starjack.png) | Sega System 1. FIRE=laser, controller B=short-range shot |
| Regulus (regulus.zip)         | ![Regulus marquee](/logos/regulus.png) | ![Regulus screenshot](/images/regulus.png) | Sega System 1, unencrypted US set. FIRE=laser, START=bomb |
| Boulder Dash (cbdash.zip)     | ![Boulder Dash marquee](/logos/cbdash.png) | ![Boulder Dash screenshot](/images/cbdash.png) | Data East DECO Cassette version. Slow tape loader removed. Experimental smooth scrolling. C64 SID music: romszip/Boulder_Dash.sid (HVSC) |
| Up'n Down (upndown.zip)       | ![Up'n Down marquee](/logos/upndown.png) | ![Up'n Down screenshot](/images/upndown.png) | Sega System 1, unencrypted US set upndownu. FIRE=jump |
| Phozon (phozon.zip)           | ![Phozon marquee](/logos/phozon.png) | ![Phozon screenshot](/images/phozon.png) |  |
| Green Beret (gberet.zip)      | ![Green Beret marquee](/logos/gberet.png) | ![Green Beret screenshot](/images/gberet.png) | FIRE=knife, START=special weapon |
| H.E.R.O. (H.E.R.O..zip)       | ![H.E.R.O. marquee](/logos/hero.png) | ![H.E.R.O. screenshot](/images/hero.png) | Atari 2600 native remake: game logic rewritten in C++, levels and graphics from the Activision cartridge ROM (romszip/H.E.R.O..zip or H.E.R.O.-pal.bin). No 6502/TIA emulation. UP=fly, DOWN=dynamite, FIRE=laser |
| Ghosts'n Goblins (gng.zip)    | ![Ghosts'n Goblins marquee](/logos/gng.png) | ![Ghosts'n Goblins screenshot](/images/gng.png) | FIRE=lance, START=jump. C64 SID music: romszip/Ghosts_n_Goblins.sid (HVSC) |
| Time Pilot '84 (tp84.zip)     | ![Time Pilot '84 marquee](/logos/tp84.png) | ![Time Pilot '84 screenshot](/images/tp84.png) | FIRE=fire, START=missile |
| Defender (defender.zip)       | ![Defender marquee](/logos/defender.png) | ![Defender screenshot](/images/defender.png) | Up/down=altitude, left/right=thrust (reverses first), FIRE=fire, START=smart bomb, START+left/right=hyperspace. 292 columns merged into 239 |
| Robotron: 2084 (robotron.zip) | ![Robotron: 2084 marquee](/logos/robotron.png) | ![Robotron: 2084 screenshot](/images/robotron.png) | Twin sticks on one stick: FIRE aims where the stick points and locks the aim while held. Controller X/B/A/Y fire up/right/down/left |
| Joust (joust.zip)             | ![Joust marquee](/logos/joust.png) | ![Joust screenshot](/images/joust.png) | 1 player. Left/right=run, FIRE=flap |
| Rafflesia (raflesia.zip)      | ![Rafflesia marquee](/logos/raflesia.png) | ![Rafflesia screenshot](/images/raflesia.png) | Sega System 1. FIRE=fire |
| SWAT (swat.zip)               | ![SWAT marquee](/logos/swat.png) | ![SWAT screenshot](/images/swat.png) | Sega System 1. FIRE=fire, START=bomb |
| Black Widow (bwidow.zip)      | ![Black Widow marquee](/logos/bwidow.png) | ![Black Widow screenshot](/images/bwidow.png) | Twin sticks on one stick: FIRE aims where the stick points and locks the aim while held. Controller X/B/A/Y fire up/right/down/left |
| 1943 (1943.zip)               | ![1943 marquee](/logos/1943.png) | ![1943 screenshot](/images/1943.png) | Bootleg set 1943b (no MCU) from the merged 1943.zip. FIRE=fire, START=special attack |
| Dig Dug II (digdug2.zip)      | ![Dig Dug II marquee](/logos/digdug2.png) | ![Dig Dug II screenshot](/images/digdug2.png) | FIRE=pump, START=jackhammer |
| Gorf (gorf.zip)               | ![Gorf marquee](/logos/gorf.png) | ![Gorf screenshot](/images/gorf.png) | Votrax SC-01 speech, needs votrsc01.zip. 320 rows merged into 288. FIRE=fire |
| Solomon's Key (solomon.zip)   | ![Solomon's Key marquee](/logos/solomon.png) | ![Solomon's Key screenshot](/images/solomon.png) | FIRE=magic, START=fireball. 8 px cropped per side; build flag SOLOMON_SCALE drops every 16th column, SOLOMON_SCROLL moves the view with the player |
| Flicky (flicky.zip)           | ![Flicky marquee](/logos/flicky.png) | ![Flicky screenshot](/images/flicky.png) | Sega System 1, unencrypted set flickys2. FIRE=jump |
| Pitfall II (pitfall2u.zip)    | ![Pitfall II marquee](/logos/pitfall2.png) | ![Pitfall II screenshot](/images/pitfall2.png) | Sega System 1, unencrypted US set, needs parent pitfall2.zip. The view follows the player. FIRE=jump |
| Mister Viking (mrviking.zip)  | ![Mister Viking marquee](/logos/mrviking.png) | ![Mister Viking screenshot](/images/mrviking.png) | Sega System 1. FIRE=throw knife, START=weapon |
| I'm Sorry (imsorry.zip)       | ![I'm Sorry marquee](/logos/imsorry.png) | ![I'm Sorry screenshot](/images/imsorry.png) | Sega System 1. FIRE=button 1, START=button 2 |
| Wonder Boy (wboy.zip)         | ![Wonder Boy marquee](/logos/wboy.png) | ![Wonder Boy screenshot](/images/wboy.png) | Sega System 1, unencrypted set wboy2u. FIRE=jump, START=attack |
| 4-D Warriors (4dwarrio.zip)   | ![4-D Warriors marquee](/logos/fourdwarrio.png) | ![4-D Warriors screenshot](/images/fourdwarrio.png) | Sega System 1. FIRE=button 1, START=button 2 |
| Heavy Metal (hvymetal.zip)    | ![Heavy Metal marquee](/logos/hvymetal.png) | ![Heavy Metal screenshot](/images/hvymetal.png) | Sega System 1. FIRE=button 1, START=button 2 |
| Super Xevious (sxevious.zip)  | ![Super Xevious marquee](/logos/sxevious.png) | ![Super Xevious screenshot](/images/sxevious.png) | Needs xevious.zip |
| Mikie (mikie.zip)             | ![Mikie marquee](/logos/mikie.png) | ![Mikie screenshot](/images/mikie.png) | FIRE=button 1, START=button 2 |
| Vulgus (vulgus.zip)           | ![Vulgus marquee](/logos/vulgus.png) | ![Vulgus screenshot](/images/vulgus.png) | FIRE=shot, START=bomb |
| Cosmic Avenger (cavenger.zip) | ![Cosmic Avenger marquee](/logos/cavenger.png) | ![Cosmic Avenger screenshot](/images/cavenger.png) | FIRE=fire, START=bomb |
| Gardia (gardia.zip)           | ![Gardia marquee](/logos/gardia.png) | ![Gardia screenshot](/images/gardia.png) | Sega System 1. FIRE=button 1, START=button 2 |
| Pleiads (pleiads.zip)         | ![Pleiads marquee](/logos/pleiads.png) | ![Pleiads screenshot](/images/pleiads.png) | FIRE=fire |
| Arkanoid (arkanoid.zip)       | ![Arkanoid marquee](/logos/arkanoid.png) | ![Arkanoid screenshot](/images/arkanoid.png) | Game Corporation bootleg set arkangc (no MCU). Left/right=paddle, FIRE=launch/laser |
| Sinistar (sinistar.zip)       | ![Sinistar marquee](/logos/sinistar.png) | ![Sinistar screenshot](/images/sinistar.png) | Rev 3 upright. 49-way stick from the 8-way stick. FIRE=fire, START=sinibomb (EXTRA without COIN pin) |
| Yie Ar Kung-Fu (yiear.zip)    | ![Yie Ar Kung-Fu marquee](/logos/yiear.png) | ![Yie Ar Kung-Fu screenshot](/images/yiear.png) | VLM5030 speech. FIRE=punch, START=kick |
| Track & Field (trackfld.zip)  | ![Track & Field marquee](/logos/trackfld.png) | ![Track & Field screenshot](/images/trackfld.png) | VLM5030 speech. Left/right=run, FIRE=jump |
| Tron (tron.zip)               | ![Tron marquee](/logos/tron.png) | ![Tron screenshot](/images/tron.png) | Also needs midssio.zip. The aim dial turns toward the stick direction. FIRE=fire |
| Discs of Tron (dotron.zip)    | ![Discs of Tron marquee](/logos/dotron.png) | ![Discs of Tron screenshot](/images/dotron.png) | Also needs midssio.zip. Auto-aim at the opponent. FIRE=throw |
| Elevator Action (elevator.zip) | ![Elevator Action marquee](/logos/elevator.png) | ![Elevator Action screenshot](/images/elevator.png) | Bootleg set elevatorb (no MCU). FIRE=shoot, UP+FIRE=jump (controller B=jump) |
| Major Havoc (mhavoc.zip)      | ![Major Havoc marquee](/logos/mhavoc.png) | ![Major Havoc screenshot](/images/mhavoc.png) | Rev 3. Left/right=roller, FIRE=fire/jump, DOWN=shield (controller A=fire, B=shield). No start button: START presses FIRE. Small text enlarged |
| Satan's Hollow (shollow.zip)  | ![Satan's Hollow marquee](/logos/shollow.png) | ![Satan's Hollow screenshot](/images/shollow.png) | Also needs midssio.zip. FIRE=fire, UP/DOWN=shield (controller B=shield) |
| Motos (motos.zip)             | ![Motos marquee](/logos/motos.png) | ![Motos screenshot](/images/motos.png) | Dig Dug II board. FIRE=power, START=jump (controller B=jump) |
| Jungle Hunt (junglek.zip)     | ![Jungle Hunt marquee](/logos/jungleh.png) | ![Jungle Hunt screenshot](/images/jungleh.png) | Set jungleh (US) from junglek.zip, Taito SJ board without MCU. FIRE=jump/stab |
| Mega Zone (megazone.zip)      | ![Mega Zone marquee](/logos/megazone.png) | ![Mega Zone screenshot](/images/megazone.png) | FIRE=fire |
| Choplifter (choplift.zip)     | ![Choplifter marquee](/logos/choplift.png) | ![Choplifter screenshot](/images/choplift.png) | Set chopliftu (unprotected), Sega System 2. FIRE=shoot, START=turn (controller B=turn) |
| Toki no Senshi (Chrono Soldier) (tokisens.zip)      | ![Tokisens marquee](/logos/tokisens.png) | ![Tokisens screenshot](/images/tokisens.png) | FIRE=fire |
| Tapper (tapper.zip)           | ![Tapper marquee](/logos/tapper.png) | ![Tapper screenshot](/images/tapper.png) | Set "Tapper (Budweiser, set 1)", files tappg0.bin etc. Also needs midssio.zip. FIRE=fire |
| Mario Bros. (mario.zip)       | ![Mario Bros. marquee](/logos/mario.png) | ![Mario Bros. screenshot](/images/mario.png) | 256 columns shown in a 240 column window that pans after player 1 (`MARIO_SCROLL`, alternatives `MARIO_SCALE`, `MARIO_CROP` in config.h). FIRE or UP=jump |
| Mr. Do's Castle (docastle.zip) | ![Mr. Do's Castle marquee](/logos/docastle.png) | ![Mr. Do's Castle screenshot](/images/docastle.png) | FIRE=hammer |
| Super Zaxxon (szaxxon.zip)    | ![Super Zaxxon marquee](/logos/szaxxon.png) | ![Super Zaxxon screenshot](/images/szaxxon.png) | Same audio as Zaxxon, needs zaxxon-audio.zip. FIRE=fire |
| Contra (contra.zip)           | ![Contra marquee](/logos/contra.png) | ![Contra screenshot](/images/contra.png) | Music and effects from the C64 port: needs Gryzor.sid (HVSC MUSICIANS/B/Brooke_Jason) in romszip/. FIRE=fire, START=jump (controller B=jump) |
| Mr. Goemon (mrgoemon.zip)     | ![Mr. Goemon marquee](/logos/mrgoemon.png) | ![Mr. Goemon screenshot](/images/mrgoemon.png) | Needs gberet.zip. FIRE=pipe, START=throw (controller B/L1/R1=throw) |
| Jail Break (jailbrek.zip)     | ![Jail Break marquee](/logos/jailbrek.png) | ![Jail Break screenshot](/images/jailbrek.png) | FIRE=shoot, START=weapon select (controller B/L1/R1=weapon select), 8 columns cropped left and right |
| Scooter Shooter (scotrsht.zip) | ![Scooter Shooter marquee](/logos/scotrsht.png) | ![Scooter Shooter screenshot](/images/scotrsht.png) | FIRE=shoot |
| Q\*bert (qbert.zip)           | ![Q*bert marquee](/logos/qbert.png) | ![Q*bert screenshot](/images/qbert.png) | US set 1. Also needs votrsc01.zip (speech). Stick turned 45 degrees: UP=up-right, RIGHT=down-right, DOWN=down-left, LEFT=up-left; diagonals map directly |

### ...
