#!/usr/bin/env bash
# usage: [GAMES=a,b] [DEFINES="A=1 B"] build.sh ENV [extra cmake args]   run inside devbox
# ENV: an rp2350 row of [envs] in firmware.toml (e.g. pico2w_dvi-remote).
# Dev builds, as with PlatformIO: GAMES replaces the env's games (own build dir),
# DEFINES adds defines (e.g. DEBUG_TIMING, GALAGINO_CLOCK_KHZ=252000).
# Needs PICO_SDK_PATH, FREERTOS_KERNEL_PATH and, for the Pico 2 W pad, BLUEPAD32_PATH
# (defaults below; Bluepad32 tag 4.2.0, pico-sdk submodules lib/btstack and lib/cyw43-driver).
set -e
here=$(cd "$(dirname "$0")" && pwd)
env=${1:?usage: build.sh ENV (an rp2350 row of [envs] in firmware.toml)}
shift
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/src/external/pico-deps/pico-sdk}
export FREERTOS_KERNEL_PATH=${FREERTOS_KERNEL_PATH:-$HOME/src/external/pico-deps/FreeRTOS-Kernel}
export BLUEPAD32_PATH=${BLUEPAD32_PATH:-$HOME/src/external/pico-deps/bluepad32}
cfg=$(GAMES=$GAMES DEFINES=$DEFINES python3 "$here/../../tools/config_to_cmake.py" "$env")
# a comma in the build path breaks the linker map option
out=$here/build/$env${GAMES:+-${GAMES//,/-}}
mkdir -p "$out"
printf '%s\n' "$cfg" > "$out/galagino.cmake"
cmake -S "$here" -B "$out" -G Ninja -DGALAGINO_CMAKE="$out/galagino.cmake" "$@"
cmake --build "$out" -- -k 0
