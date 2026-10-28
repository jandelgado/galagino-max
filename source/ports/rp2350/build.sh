#!/usr/bin/env bash
# usage: [BOARD=pico2w_dvi] build.sh [GAME[,GAME...]] [extra cmake args]   (default pacman); run inside devbox
# BOARD: src/boards/ profile (pico2, pico2w_dvi, feather_rp2350), default pico2.
# Several games give the menu. CLOCK_KHZ=252000 overclocks (and uses its own build dir);
# DVI profiles set 252 MHz themselves.
# Needs PICO_SDK_PATH and FREERTOS_KERNEL_PATH (defaults below).
set -e
here=$(cd "$(dirname "$0")" && pwd)
game=${1:-pacman}
board=${BOARD:-pico2}
[ $# -gt 0 ] && shift
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/src/external/pico-deps/pico-sdk}
export FREERTOS_KERNEL_PATH=${FREERTOS_KERNEL_PATH:-$HOME/src/external/pico-deps/FreeRTOS-Kernel}
out=$here/build/$board-$game${CLOCK_KHZ:+-$CLOCK_KHZ}
mkdir -p "$out"
python3 "$here/../../tools/pio_to_cmake.py" azdelivery-local --only "$game" --hal rp2350 --no-net \
  --layout rp2350 --define DEBUG_TIMING ${CLOCK_KHZ:+--define GALAGINO_CLOCK_KHZ=$CLOCK_KHZ} > "$out/galagino.cmake"
cmake -S "$here" -B "$out" -G Ninja -DGALAGINO_CMAKE="$out/galagino.cmake" -DGALAGINO_BOARD="$board" "$@"
cmake --build "$out" -- -k 0
