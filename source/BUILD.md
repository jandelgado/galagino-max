# Build notes

ESP-IDF 6.1 (PlatformIO `espidf` framework), no Arduino core, no FastLED. Every SDK call sits in
`src/hal/esp/` (contract: `src/hal/CONTRACT.md`). Build and flash: see the README.

Envs: every build is a row of `[envs]` in `firmware.toml`, `<board>-<mode>` with the same
parameters on every platform: `platform` (`esp`, `rp2350`, `host`), `board` (`BOARD_<NAME>`,
profile `src/boards/<board>.h`), `mode` (`[modes]`: games and defines) and `layout` (`[arena]`).
`tools/firmware_config.py` turns a row into the defines for both builds: PlatformIO
(`extraScript.py`, an `[env:<name>]` in `platformio.ini` adds the PlatformIO board, partitions
and linker flags) and RP2350 (`ports/rp2350/build.sh <name>`). Dev builds, both platforms:
`GAMES=a,b` replaces the env's games, `DEFINES="A=1 B"` adds defines (e.g. `DEBUG_TIMING`).

PlatformIO inputs live in `pio/`: board definitions (`pio/boards/`), partition tables
(`pio/partitions/`) and the IDF configuration (`pio/sdkconfigs/`): `sdkconfig.defaults` (240 MHz,
QIO 80 MHz flash, 4 MB, 1000 Hz tick, `-Os`, log level warn, BT off), `sdkconfig.defaults.16mb`
for 16 MB boards (`board_build.cmake_extra_args`), `sdkconfig.defaults.<layout>` for an Arena
layout that needs its own task stacks (appended by `extraScript.py` for the env's layout, e.g.
`esp32_local`). Also `CMakeLists.txt`, `src/CMakeLists.txt`. The generated sdkconfig is
`.pio/build/<env>/sdkconfig`: delete it after changing any defaults file, or the old values stay
(the hook stops the build when it disagrees with the layout file and names the file to delete).

Arena layout: `[arena]` in `firmware.toml` lists the block sizes per layout (`name = [A, B]`
bytes), every env selects one. It becomes `ARENA_LAYOUT_<NAME>`, `ARENA_A_SIZE` and
`ARENA_B_SIZE` (RP2350: `ports/rp2350/ram_check.ld` fails the link when the heap left next to
the image's RAM code cannot hold the Arena). romconv plans every layout; a generated `*_arena_plan.h` or `*_bundle.h`
without a block for the build's layout is an `#error`, stale sizes fail a `static_assert`:
after editing a row, rerun romconv. Native tests build with `esp32_local`. A `_remote` layout
in a local env (or the reverse) stops the build. `DEFINES=DEBUG_ARENA` prints `Asset: late unpack` when a COMPRESSED asset's first access
comes after the emulation task spawned: machines take `data()` in the ctor or `start()`.

| Subsystem | Implementation |
| --- | --- |
| entry | `app_main()` runs the frame loop on core 0 |
| audio, built-in DAC | `dac_continuous` (SIMUL for mono, ALTER for `SND_DIFF`) |
| audio, ES8311 | `i2s_std` Philips, signed; codec setup over `hal::I2c` |
| LEDs | RMT WS2812 |
| I2C | `i2c_master` (controller, Nunchuk, ES8311 share port 0) |
| WiFi (remote mode) | `esp_wifi`, driver heap in Arena, `-Wl,--wrap=free` |

RP2350 Bluetooth pad (Pico 2 W DVI default input, `HAL_GAMEPAD_BT`): BTstack from the pico-sdk
2.2.0 submodules (`lib/btstack`, `lib/cyw43-driver`; `lib/lwip` only for the CMake targets) and
Bluepad32 4.2.0 (`BLUEPAD32_PATH`, default `sdks/bluepad32`), compiled into
the image by `ports/rp2350/bt/bluepad32.cmake` with the configs next to it (one classic pad,
BLE off). Costs ~20 KB RAM and ~406 KB flash (232 KB of it the CYW43 firmware). Link keys live
in pico_btstack's flash bank, the last 12 KB of flash: `ram_check.ld` fails the link when an
image reaches into it. With a wired source (`-D NUNCHUCK_INPUT`, ...) the linker drops the BT
code.

RP2350 remote mode (Pico 2 W DVI only): env `pico2w_dvi-remote`. WiFi over the same CYW43
with lwIP (`ports/rp2350/lwipopts.h`, static, ~20 KB). WiFi and server: `GALAGINO_WIFI_SSID/PASS` and `GALAGINO_ASSETS_URL` in
`config_local.h`. The asset cache
is all flash between the image (4 KB up) and the Bluetooth key bank (~1.3 MB with every
built-in game); erase and write park the emulation core in RAM so the HDMI scanout keeps
running. Bluetooth inquiry scans pause while WiFi is up (they take the shared radio).

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
