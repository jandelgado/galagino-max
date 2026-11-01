#!/usr/bin/env bash
# usage: [GAMES=a,b] [DEFINES="A=1 B"] build.sh ENV [extra cmake args]   run inside devbox
# ENV: an rp2350 row of [envs] in firmware.toml (e.g. pico2w_dvi-remote).
# Dev builds, as with PlatformIO: GAMES replaces the env's games (own build dir),
# DEFINES adds defines (e.g. DEBUG_TIMING, GALAGINO_CLOCK_KHZ=252000).
# SDKs from <repo>/sdks (sdks.sh clones them) unless PICO_SDK_PATH,
# FREERTOS_KERNEL_PATH or BLUEPAD32_PATH (Pico 2 W pad) are set.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdks=$(cd "$here/../../.." && pwd)/sdks
env=${1:?usage: build.sh ENV (an rp2350 row of [envs] in firmware.toml)}
shift
export PICO_SDK_PATH=${PICO_SDK_PATH:-$sdks/pico-sdk}
export FREERTOS_KERNEL_PATH=${FREERTOS_KERNEL_PATH:-$sdks/FreeRTOS-Kernel}
export BLUEPAD32_PATH=${BLUEPAD32_PATH:-$sdks/bluepad32}
[ -d "$PICO_SDK_PATH" ] || { echo "no pico-sdk at $PICO_SDK_PATH: run $here/sdks.sh or set PICO_SDK_PATH" >&2; exit 1; }
cfg=$(GAMES=$GAMES DEFINES=$DEFINES python3 "$here/../../tools/config_to_cmake.py" "$env")
# a comma in the build path breaks the linker map option
out=$here/build/$env${GAMES:+-${GAMES//,/-}}
mkdir -p "$out"
printf '%s\n' "$cfg" > "$out/galagino.cmake"
cmake -S "$here" -B "$out" -G Ninja -DGALAGINO_CMAKE="$out/galagino.cmake" "$@"
cmake --build "$out" -- -k 0
