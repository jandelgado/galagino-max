#!/usr/bin/env bash
# Menu logo source: MAME snapshot of level 1 screen 1 (hero, miner, walls),
# playfield cropped to 160 x 69, scaled to 224 x 96, black transparent.
#   make_logo.sh <rom.a26> <out.png>
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf '1 -\n100 S\n106 -\nP 200 9c 1\nP 200 a0 9\nP 201 9c 1\nP 201 a0 9\n' > "$work/script.txt"
H_SCRIPT="$work/script.txt" H_FRAMES=203 H_SNAP=203 "$here/run_mame.sh" "$1" "$work" >/dev/null
ffmpeg -loglevel error -y -i "$work/snap_00203.png" \
  -vf "crop=160:69:8:44,scale=224:96:flags=neighbor,format=rgba,colorkey=black:0.01:0" "$2"
echo "$2"
