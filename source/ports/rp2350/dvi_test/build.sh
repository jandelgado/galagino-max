#!/usr/bin/env bash
# usage: dvi_test/build.sh [BOARD]   (default pico2w_dvi); run inside devbox
# Output: ports/rp2350/build/dvi_test-<board>/dvi_test.uf2
set -e
here=$(cd "$(dirname "$0")" && pwd)
board=${1:-pico2w_dvi}
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/src/external/pico-deps/pico-sdk}
out=$here/../build/dvi_test-$board
cmake -S "$here" -B "$out" -G Ninja -DGALAGINO_BOARD="$board"
cmake --build "$out"
