#!/usr/bin/env bash
# usage: [BOARD=pico2w_dvi] build.sh [GAME[,GAME...]] [extra cmake args]   (default pacman); run inside devbox
# BOARD: src/boards/ profile (pico2, pico2w_dvi, feather_rp2350), default pico2.
# Several games give the menu. CLOCK_KHZ=252000 overclocks (and uses its own build dir);
# DVI profiles set 252 MHz themselves. DEFINES="A=1 B" adds build defines
# (e.g. DEFINES=MASTER_ATTRACT_GAME_TIMEOUT=120000).
# Needs PICO_SDK_PATH and FREERTOS_KERNEL_PATH (defaults below).
set -e
here=$(cd "$(dirname "$0")" && pwd)
game=${1:-pacman}
board=${BOARD:-pico2}
[ $# -gt 0 ] && shift
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/src/external/pico-deps/pico-sdk}
export FREERTOS_KERNEL_PATH=${FREERTOS_KERNEL_PATH:-$HOME/src/external/pico-deps/FreeRTOS-Kernel}
# a comma in the build path breaks the linker map option
out=$here/build/$board-${game//,/-}${CLOCK_KHZ:+-$CLOCK_KHZ}
mkdir -p "$out"
# the DVI framebuffer takes part of Arena block B
layout=rp2350
grep -q "define HAL_VIDEO_DVI_HSTX" "$here/../../src/boards/$board.h" && layout=rp2350_dvi
python3 "$here/../../tools/pio_to_cmake.py" azdelivery-local --only "$game" --hal rp2350 --no-net \
  --layout $layout --define DEBUG_TIMING --define DEBUG_TIMING_FPS_HUD ${CLOCK_KHZ:+--define GALAGINO_CLOCK_KHZ=$CLOCK_KHZ} \
  $(for d in $DEFINES; do printf -- '--define %s ' "$d"; done) > "$out/galagino.cmake"
cmake -S "$here" -B "$out" -G Ninja -DGALAGINO_CMAKE="$out/galagino.cmake" -DGALAGINO_BOARD="$board" "$@"
cmake --build "$out" -- -k 0
