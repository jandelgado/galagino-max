# Build notes

ESP-IDF 6.1 (PlatformIO `espidf` framework), no Arduino core, no FastLED. Every SDK call sits in
`src/hal/esp/` (contract: `src/hal/CONTRACT.md`). Build and flash: see the README.

Config: `sdkconfig.defaults` (240 MHz, QIO 80 MHz flash, 4 MB, 1000 Hz tick, `-Os`, log level
warn, BT off), `sdkconfig.defaults.16mb` for 16 MB boards (`board_build.cmake_extra_args`),
`CMakeLists.txt`, `src/CMakeLists.txt`. `sdkconfig.<env>` is generated and git-ignored: delete
it after changing the defaults.

| Subsystem | Implementation |
| --- | --- |
| entry | `app_main()` runs the frame loop on core 0 |
| audio, built-in DAC | `dac_continuous` (SIMUL for mono, ALTER for `SND_DIFF`) |
| audio, ES8311 | `i2s_std` Philips, signed; codec setup over `hal::I2c` |
| LEDs | RMT WS2812 |
| I2C | `i2c_master` (controller, Nunchuk, ES8311 share port 0) |
| WiFi (remote mode) | `esp_wifi`, driver heap in Arena, `-Wl,--wrap=free` |

## Pitfalls

- `src/CMakeLists.txt` must list C sources first: PlatformIO's espidf builder orders the C/C++
  compile groups by the first source; a `.cpp` first hands C-only options to every C++ file.
- IDF builds every component with `-fno-jump-tables`; `src/` re-enables jump tables (CPU opcode
  switches are 25-40 % slower without).
- IDF compiles with `-Werror=all`; the envs pass `-w` (the emulator cores are not warning-free).
- DAC mono must use `DAC_CHANNEL_MODE_SIMUL` (ALTER plays at double speed) and samples in order
  (the old Arduino driver's pair swap distorts sampled sounds).
- Some CYD boards ship with a broken speaker amplifier stage (R7/R8 fitted as 0 Ohm): very
  loud, distorted and noisy, software volume has little effect. Not a firmware bug; fix it with
  the resistor mod: https://github.com/hexeguitar/ESP32_TFT_PIO#audio-amp-gain-mod (details:
  https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/discussions/356).
- Two tasks of equal priority on the video core: the busy one must `hal::yield()`, else the main
  task waits out a 1 ms slice after every SPI wake-up.
- Static constructors run before the scheduler: no `vTaskDelay` in them.
- Two pio runs in one project at once (a device build during `pio test`) break each other's
  `.pio` build directories.
