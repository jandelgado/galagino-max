# Build notes

ESP-IDF 6.1 (PlatformIO `espidf` framework), no Arduino core, no FastLED. Every SDK call sits in
`src/hal/esp/` (contract: `src/hal/CONTRACT.md`). Build and flash: see the README.

Config: `sdkconfig.defaults` (240 MHz, QIO 80 MHz flash, 4 MB, 1000 Hz tick, `-Os`, log level
warn, BT off), `sdkconfig.defaults.16mb` for 16 MB boards (`board_build.cmake_extra_args`),
`sdkconfig.defaults.<layout>` for an Arena layout that needs its own task stacks (appended by
`extraScript.py` for the env's `custom_arena_layout`, e.g. `esp32_local`), `CMakeLists.txt`,
`src/CMakeLists.txt`. `sdkconfig.<env>` is generated and git-ignored: delete it after changing
any defaults file, or the old values stay (the hook stops the build when it disagrees with
the layout file and names the file to delete).

Arena layout: `[arena]` in `platformio.ini` lists the block sizes per layout (`name = A B`
bytes), every env selects one with `custom_arena_layout`. `extraScript.py` turns it into
`ARENA_LAYOUT_<NAME>`, `ARENA_A_SIZE` and `ARENA_B_SIZE` (`tools/pio_to_cmake.py --layout` for
the RP2350 build: `ports/rp2350/build.sh` picks `rp2350_dvi` for DVI profiles, `rp2350`
otherwise; `ports/rp2350/ram_check.ld` fails the link when the heap left next to the image's
RAM code cannot hold the Arena). romconv plans every layout; a generated `*_arena_plan.h` or `*_bundle.h`
without a block for the build's layout is an `#error`, stale sizes fail a `static_assert`:
after editing a row, rerun romconv. Native tests build with `esp32_local`. A `_remote` layout
in a local env (or the reverse) stops at the hook. `-D DEBUG_ARENA` (e.g. in
`PLATFORMIO_BUILD_FLAGS`) prints `Asset: late unpack` when a COMPRESSED asset's first access
comes after the emulation task spawned: machines take `data()` in the ctor or `start()`.

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
