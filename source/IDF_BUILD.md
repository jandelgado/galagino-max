# ESP-IDF only build

Builds galagino with ESP-IDF 6.1 (PlatformIO `espidf` framework), without the Arduino core and
FastLED. Same sources as the Arduino build; only `src/hal/esp/` differs (`#ifdef ARDUINO`).

```
pio run -e idf-azdelivery-local -t upload -t monitor
```

Config: `sdkconfig.defaults` (240 MHz, QIO 80 MHz flash, 1000 Hz tick, size optimization, log
level warn, BT off), `CMakeLists.txt`, `src/CMakeLists.txt`. `sdkconfig.idf*` is generated and
git-ignored. Delete it after changing `sdkconfig.defaults`.

## Differences to the Arduino build

| Subsystem | Arduino | IDF only |
| --- | --- | --- |
| entry | `setup()`/`loop()` | `app_main()` runs the loop on core 0 |
| audio | legacy I2S + built-in DAC | `dac_continuous` (SIMUL for mono, ALTER for `SND_DIFF`) |
| ES8311 audio | legacy I2S, left-justified, offset binary | `i2s_std` Philips, signed (`audio_i2s.cpp`, untested on a device) |
| LEDs | FastLED | RMT WS2812 (`led_strip.cpp`) |
| I2C | `Wire` | `i2c_master` |
| IDF switch tables | on | forced off by IDF, re-enabled for `src/` (CPU opcode switches are 25-40 % slower without) |

Not ported: WiFi/remote mode (`src/net/`), Nunchuk. Envs: `idf-azdelivery-local`, `idf-fnk0104-full`.

## Results (azDelivery DevKitV4, DEBUG_TIMING single-game builds, attract mode)

| | Arduino + HAL | IDF only |
| --- | --- | --- |
| flash (azdelivery-local) | 4124097 B | 3917475 B |
| static RAM | 24908 B | 14376 B |
| free heap in game (gyruss) | 68428 B | 107508 B |
| pacman Hz / Cpu / Video | 60 / 4.0 / 12.0 ms | 60 / 4.0 / 12.0 ms |
| starwars | 60 / 6.6 / 12.9 ms | 60 / 6.0 / 13.0 ms |
| gyruss | 60 / 13.3 / 12.7 ms | 60 / 10.6 / 12.5 ms |

Cpu = `run_frame()` per frame, Video = frame start to last strip sent.

## Pitfalls found

- DAC mono must use `DAC_CHANNEL_MODE_SIMUL`: ALTER on the ESP32 consumes two words per sample
  and plays at double speed.
- Two tasks of equal priority on the video core (gyruss audio task, main task): the busy task
  must `hal::yield()`, else the main task waits out a 1 ms slice after every SPI wake-up.
- C sources must come first in `src/CMakeLists.txt`: PlatformIO's espidf builder orders the C/C++
  compile groups by the first source, and a `.cpp` first (e.g. a new `src/audio/`) hands the C-only
  `-Wno-old-style-declaration` to every C++ file, which `-Werror` rejects.
- Static constructors run before the scheduler: no `vTaskDelay` in them (display reset is in
  `begin()`).
