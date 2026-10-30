#!/usr/bin/env bash
# usage: [BOARD=pico2w_dvi] [MODE=local] build.sh [GAME[,GAME...]] [extra cmake args]   (default pacman); run inside devbox
# BOARD: src/boards/ profile (pico2, pico2w_dvi, feather_rp2350), default pico2.
# MODE: firmware.toml [modes] entry, default local; remote picks the layout's _remote row
# and, without GAME, builds the mode's own games (the built-in set).
# Several games give the menu. CLOCK_KHZ=252000 overclocks (and uses its own build dir);
# DVI profiles set 252 MHz themselves. DEFINES="A=1 B" adds build defines
# (e.g. DEFINES=MASTER_ATTRACT_GAME_TIMEOUT=120000, DEFINES=DEBUG_TIMING_FPS_HUD for the
# FPS overlay).
# Needs PICO_SDK_PATH, FREERTOS_KERNEL_PATH and, for the Pico 2 W pad, BLUEPAD32_PATH
# (defaults below; Bluepad32 tag 4.2.0, pico-sdk submodules lib/btstack and lib/cyw43-driver).
set -e
here=$(cd "$(dirname "$0")" && pwd)
board=${BOARD:-pico2}
mode=${MODE:-local}
game=${1:-pacman}
[ $# -eq 0 ] && [ "$mode" = remote ] && game=builtin
[ $# -gt 0 ] && shift
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/src/external/pico-deps/pico-sdk}
export FREERTOS_KERNEL_PATH=${FREERTOS_KERNEL_PATH:-$HOME/src/external/pico-deps/FreeRTOS-Kernel}
export BLUEPAD32_PATH=${BLUEPAD32_PATH:-$HOME/src/external/pico-deps/bluepad32}
# a comma in the build path breaks the linker map option
out=$here/build/$board-${game//,/-}${CLOCK_KHZ:+-$CLOCK_KHZ}
[ "$mode" = local ] || out=$out-$mode
mkdir -p "$out"
# the DVI framebuffer takes part of Arena block B
layout=rp2350
grep -q "define HAL_VIDEO_DVI_HSTX" "$here/../../src/boards/$board.h" && layout=rp2350_dvi
[ "$mode" = remote ] && layout=${layout}_remote
python3 "$here/../../tools/config_to_cmake.py" --mode "$mode" --layout $layout --hal rp2350 \
  $([ "$game" = builtin ] || printf -- '--only %s' "$game") \
  --define DEBUG_TIMING ${CLOCK_KHZ:+--define GALAGINO_CLOCK_KHZ=$CLOCK_KHZ} \
  $(for d in $DEFINES; do printf -- '--define %s ' "$d"; done) > "$out/galagino.cmake"
cmake -S "$here" -B "$out" -G Ninja -DGALAGINO_CMAKE="$out/galagino.cmake" -DGALAGINO_BOARD="$board" "$@"
cmake --build "$out" -- -k 0
