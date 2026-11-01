#!/usr/bin/env bash
# usage: build.sh [GAME] [extra cmake args]   (default pacman); run inside devbox
# CLOCK_KHZ=252000 overclocks (and uses its own build dir).
# Needs PICO_SDK_PATH and FREERTOS_KERNEL_PATH (defaults below).
set -e
here=$(cd "$(dirname "$0")" && pwd)
game=${1:-pacman}
[ $# -gt 0 ] && shift
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/src/external/pico-deps/pico-sdk}
export FREERTOS_KERNEL_PATH=${FREERTOS_KERNEL_PATH:-$HOME/src/external/pico-deps/FreeRTOS-Kernel}
out=$here/build/$game${CLOCK_KHZ:+-$CLOCK_KHZ}
mkdir -p "$out"
python3 "$here/../../tools/pio_to_cmake.py" azdelivery-local --only "$game" --hal rp2350 --no-net \
  --layout rp2350 --define DEBUG_TIMING ${CLOCK_KHZ:+--define GALAGINO_CLOCK_KHZ=$CLOCK_KHZ} > "$out/galagino.cmake"
cmake -S "$here" -B "$out" -G Ninja -DGALAGINO_CMAKE="$out/galagino.cmake" "$@"
cmake --build "$out" -- -k 0
